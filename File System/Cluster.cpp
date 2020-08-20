#include "Cluster.h"

Cluster::Cluster(Partition *p, ClusterNo c) : partition(p), clusterNo(c) {
	data = new char[ClusterSize];
	partition->readCluster(clusterNo, data);
}

Cluster::~Cluster() {
	save();
	delete[] data;
	data = nullptr;
}
