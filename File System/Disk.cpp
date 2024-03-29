#include "Disk.h"

Disk::Disk(Partition *p) : partition(p) {
	// Number of bits for bit vector is equal to number of clusters
	ClusterNo bitsForBitVector = partition->getNumOfClusters();

	// bytesForBitVector = ceil(bitsForBitVector / 8);
	ClusterNo bytesForBitVector = ((bitsForBitVector >> 3) << 3) == bitsForBitVector ?
		bitsForBitVector >> 3 : (bitsForBitVector >> 3) + 1;

	// clustersForBitVector = ceil(bytesForBitVector / 2048);  where 2048 == ClusterSize
	ClusterNo clustersForBitVector = ((bytesForBitVector >> 11) << 11) == bytesForBitVector ?
		bytesForBitVector >> 11 : (bytesForBitVector >> 11) + 1;

	bitVector = new BitVector(partition, clustersForBitVector);

	// Bit vector occupies clusters 0, 1, ..., clustersForBitVector - 1,
	// so first-level index cluster of root directory has ordinal number == clustersForBitVector
	ClusterNo rootDirCluster = clustersForBitVector;

	firstLevelDirectory = new Cluster(partition, rootDirCluster);
}

Disk::~Disk() {
	delete firstLevelDirectory;
	firstLevelDirectory = nullptr;

	delete bitVector;
	bitVector = nullptr;

	delete partition;
	partition = nullptr;
}

bool Disk::initializeBitVector() {
	// Initially, all clusters are free except clusters for bit vector and one cluster for first-level index of root dir
	// Free cluster has bit value 1, and occupied cluster has bit value 0

	ClusterNo freeStart = firstLevelDirectory->getClusterNo() + 1;  // first free cluster

	for (ClusterNo occupied = 0; occupied < freeStart; occupied++) {
		if (!bitVector->occupy(occupied)) {
			return false;
		}
	}

	for (ClusterNo free = freeStart; free < partition->getNumOfClusters(); free++) {
		if (!bitVector->makeFree(free)) {
			return false;
		}
	}

	return true;
}

bool Disk::initializeRootDir() {
	// Initially, every entry in first-level index of root directory is 0.
	return firstLevelDirectory->clear();
}

Cluster* Disk::getCluster(ClusterNo clusterNo) const {
	if (clusterNo >= partition->getNumOfClusters()) return nullptr;
	return new Cluster(partition, clusterNo);
}
