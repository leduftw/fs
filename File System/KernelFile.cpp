#include <algorithm>

#include "KernelFile.h"
#include "KernelFS.h"

KernelFile::KernelFile(Disk *d, ClusterNo start, ClusterNo dir, char m, BytesCnt sz) : firstLevel(d->getPartition(), start), directory(d->getPartition(), dir) {
	disk = d;
	mode = m;
	fileSize = sz;

	isClosed = false;
	cursor = 0;  // ?
}

KernelFile::~KernelFile() {
	isClosed = true;

	if (mode == 'w') {
		// Update fileSize field in file descriptor in root directory
	}
}

char KernelFile::write(BytesCnt howMany, char *buffer) {
	if (isClosed || mode == 'r' || buffer == nullptr || howMany == 0) {
		return 0;
	}

	ClusterNo target = cursor / ClusterSize;  // data cluster number where writing begins
	BytesCnt offset = cursor % ClusterSize;  // offset in target where writing begins

	BytesCnt buffRel = 0;  // position in buffer where reading begins
	bool done = false;

	IndexEntry firstLevelEntry = target / firstLevel.size();
	if ((int)firstLevelEntry >= firstLevel.size()) return 0;

	for (int i = firstLevelEntry; i < firstLevel.size(); i++) {
		if (firstLevel[i] == 0) {
			// Extend file with one second-level cluster

			ClusterNo free = disk->getBitVector()->findFreeCluster();
			if (free == 0) {  // there is no space on disk
				return 0;
			}

			disk->getBitVector()->occupy(free);
			Cluster *cluster = disk->getCluster(free);
			cluster->clear();
			// cluster->save();
			delete cluster;  // automatically saves data to disk

			firstLevel[i] = free;
		}

		Index secondLevel(disk->getPartition(), firstLevel[i]);
		IndexEntry secondLevelEntry = (i == firstLevelEntry ? target % secondLevel.size() : 0);
		for (int j = secondLevelEntry; j < secondLevel.size(); j++) {
			if (secondLevel[j] == 0) {
				// Extend file with data cluster

				ClusterNo free = disk->getBitVector()->findFreeCluster();
				if (free == 0) return 0;

				disk->getBitVector()->occupy(free);
				Cluster *cluster = disk->getCluster(free);
				cluster->clear();
				delete cluster;

				secondLevel[j] = free;
			}

			BytesCnt toWrite = min(howMany, ClusterSize - offset);

			Cluster *dataCluster = disk->getCluster(secondLevel[j]);
			char *data = dataCluster->getData();
			memcpy(data + offset, buffer + buffRel, toWrite);

			delete dataCluster;  // save data to disk

			if (eof()) {  // only if we are appending to file, fileSize grows
				fileSize += toWrite;
			}

			buffRel += toWrite;
			howMany -= toWrite;
			offset = 0;

			cursor += toWrite;

			if (howMany == 0) {
				done = true;
				break;
			}

		}

		if (done) break;
	}

	return done;
}

BytesCnt KernelFile::read(BytesCnt howMany, char *buffer) {
	if (isClosed || buffer == nullptr || eof() || getFileSize() == 0) {
		return 0;
	}

	BytesCnt bytesRead = 0;

	ClusterNo target = cursor / ClusterSize;  // data cluster number where reading begins
	BytesCnt offset = cursor % ClusterSize;  // offset in target where reading begins

	BytesCnt buffRel = 0;  // position in buffer where writing begins
	bool done = false;

	IndexEntry firstLevelEntry = target / firstLevel.size();
	if ((int)firstLevelEntry >= firstLevel.size()) return 0;

	for (int i = firstLevelEntry; i < firstLevel.size(); i++) {
		if (firstLevel[i] == 0) {  // should not happen
			done = true;
			break;
		}

		Index secondLevel(disk->getPartition(), firstLevel[i]);
		IndexEntry secondLevelEntry = (i == firstLevelEntry ? target % secondLevel.size() : 0);
		for (int j = secondLevelEntry; j < secondLevel.size(); j++) {
			if (secondLevel[j] == 0) {  // should not happen
				done = true;
				break;
			}

			BytesCnt toRead = min(min(howMany, ClusterSize - offset), fileSize - cursor);

			Cluster *dataCluster = disk->getCluster(secondLevel[j]);
			char *data = dataCluster->getData();
			memcpy(buffer + buffRel, data + offset, toRead);
			delete dataCluster;

			buffRel += toRead;
			howMany -= toRead;
			offset = 0;

			cursor += toRead;
			bytesRead += toRead;

			if (howMany == 0 || eof()) {
				done = true;
				break;
			}

		}

		if (done) break;
	}

	return bytesRead;
}

char KernelFile::seek(BytesCnt position) {
	if (isClosed || position > getFileSize()) {
		return 0;
	}

	// By setting cursor to getFileSize()
	// we ensure that eof() returns true
	cursor = position;

	return 1;
}

BytesCnt KernelFile::filePos() {
	if (isClosed) {
		return 0;  // cursor can be 0 too
	}
	return cursor;
}

char KernelFile::eof() {
	if (isClosed) {
		return 1;
	}

	if (getFileSize() == 0) return 1;  // file is empty
	return (filePos() == getFileSize() ? 2 : 0);
}

BytesCnt KernelFile::getFileSize() {
	if (isClosed) {
		return 0;  // fileSize can also be 0
	}

	return fileSize;
}

char KernelFile::truncate() {
	if (isClosed || eof() || mode == 'r') {
		return 0;
	}

	bool done = false;
	BytesCnt end = fileSize % ClusterSize ? fileSize : fileSize - 1;
	ClusterNo lastCluster = end / ClusterSize;  // last data cluster that should be freed
	ClusterNo firstCluster = cursor % ClusterSize ? (cursor / ClusterSize + 1) : (cursor / ClusterSize);  // first data cluster that should be freed

	IndexEntry firstLevelEntry = firstCluster / firstLevel.size();
	for (int i = firstLevelEntry; i < firstLevel.size(); i++) {
		if (firstLevel[i] == 0) return 0;

		Index secondLevel(disk->getPartition(), firstLevel[i]);
		IndexEntry secondLevelEntry = (i == firstLevelEntry ? firstCluster % secondLevel.size() : 0);
		for (int j = secondLevelEntry; j < secondLevel.size(); j++) {
			if (secondLevel[j] == 0) return 0;

			disk->getBitVector()->makeFree(secondLevel[j]);
			secondLevel[j] = 0;

			if (firstCluster++ >= lastCluster) {
				done = true;
				break;
			}
		}

		if (secondLevelEntry == 0) {  // then also second-level index should be freed
			disk->getBitVector()->makeFree(firstLevel[i]);
			firstLevel[i] = 0;
		}

		if (done) {
			break;
		}
	}

	// Even if whole file is deleted, we save first-level index

	// Set new fileSize
	fileSize = cursor;

	return done;
}
