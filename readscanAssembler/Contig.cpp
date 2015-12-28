#include <fstream>
#include <sstream>
#include <algorithm>    // std::reverse
#include <vector>       // std::vector
#include "Contig.h"
using std::stringstream;
using std::ofstream;

void Contig::printPairStatistics(Bloom* pair_filter){
	std::list<JuncResult> results = getJuncResults(1, 0, 3*length());
	std::cout << "Length " << length() << ", results " << results.size() << "\n";
	const int maxDist = 2000;
	int posNegPairCounts [2][maxDist/50] = {};

	for(int i = 0; i < maxDist/50; i++){
		posNegPairCounts[0][i] = 0;
		posNegPairCounts[1][i] = 0;
	}

	for(auto itL = results.begin(); itL != results.end(); itL++){
		for(auto itR = itL; itR != results.end(); itR++){
			int index = (itR->distance - itL->distance)/50;
			if(index < maxDist/50 && index >= 0){
				if(pair_filter->containsPair(JuncPair(itL->kmer, itR->kmer))){
					posNegPairCounts[0][index] += 1;
				}
				else{
					posNegPairCounts[1][index] += 1;
				}
			}
		}
	}

	printf("Pair pos/neg char, aggregated over buckets of length 50:\n");
	for(int i = 0; i < maxDist / 50; i++){
		std::cout << posNegPairCounts[0][i] << ",";
		std::cout << posNegPairCounts[1][i] << "\n";
	}
}

//Reverses if needed to get "canonical" concatenation of two in the same direction
//Reverses again at the end to ensure no mutation of contigs
Contig* Contig::concatenate(Contig* otherContig, int thisSide, int otherSide){
	if(thisSide == 1){
		reverse();
	}
	if(otherSide == 2){
		otherContig->reverse();
	}
	Contig* concatenation =  concatenate(otherContig);
	if(thisSide == 1){
		reverse();
	}
	if(otherSide == 2){
		otherContig->reverse();
	}
	return concatenation;
}

//utility for linking them if they're both facing "forward"
Contig* Contig::concatenate(Contig* otherContig){
	Contig* result = new Contig();
	result->setEnds(node1_p, ind1, otherContig->node2_p, otherContig->ind2);
	if(getSeq().length() < sizeKmer){
		printf("ERROR: seq less than k long in Contig::Concatenate.\n");
	}
	result->setContigJuncs(contigJuncs.concatenate(otherContig->contigJuncs));
	return result;
}

void Contig::reverse(){
	{ContigNode * temp = node1_p;
		node1_p = node2_p;
		node2_p = temp;}

	{int temp = ind1;
		ind1 = ind2;
		ind2 = temp;}

	contigJuncs.reverse();
}

void Contig::setEnds( ContigNode* n1, int i1, ContigNode* n2, int i2){
	node1_p = n1;
	node2_p = n2;
	setIndices(i1, i2);
	if(node1_p){
		node1_p->contigs[i1] = this;
	}
	if(node2_p){
		node2_p->contigs[i2] = this;
	}
}

//Assumes this is startDist away from the real start, so increments all by startDist
//Side refers to which side of the contig to start from
std::list<JuncResult> Contig::getJuncResults(int side, int startDist, int maxDist){
	if(side == 2){
		reverse();
	}
	auto result = contigJuncs.getJuncResults(ind1 != 4, startDist, maxDist); //forward if ind1 != 4, backward if ind1 == 4
	if(side == 2){
		reverse();
	}
	return result;
}

int Contig::length(){
	return contigJuncs.length();
}

double Contig::getAvgCoverage(){
	return contigJuncs.getAvgCoverage();
}

int Contig::getMinAdjacentCoverage(){
	if(isIsolated()) return 0;
	int min = 1000000;
	if(node1_p){
		min = node1_p->getTotalCoverage();
	}
	if(node2_p){
		min = std::min(min, node2_p->getTotalCoverage());
	}
	return min;
}

float Contig::getMass(){
	return getAvgCoverage()*getSeq().length();
}

void Contig::setIndices(int i1, int i2){
	ind1 = i1;
	ind2 = i2;
}

int Contig::getMinIndex(){
	return std::min(ind1, ind2);
}

ContigNode* Contig::otherEndNode(ContigNode * oneEnd){
	if(node1_p == oneEnd){
		return node2_p;
	}
	if(node2_p == oneEnd){
		return node1_p;
	}
	printf("ERROR: tried to get other end of a contig, but the given pointer didn't point to either end!.\n");
	std::cout << "node1_p: " << node1_p << " node2_p: " << node2_p << " oneEnd: " << oneEnd << "\n";
	return nullptr;
}

//Assumes the given contig node points to one end of this contig
kmer_type Contig::getNodeKmer(ContigNode * contigNode){
	if(node1_p == contigNode){
		return getSideKmer(1);
	}
	if(node2_p == contigNode){
		return getSideKmer(2);
	}
	printf("ERROR: tried to get the kmer corresponding to a node not adjacent to this contig from this contig.\n");
}

ContigNode* Contig::getNode(int side){
	if (side == 1){
		return node1_p;
	}
	if(side == 2){
		return node2_p;
	}
	printf("ERROR: called getNode on contignode with side other than 1,2\n");
}

int Contig::getIndex(int side){
	if (side == 1){
		return ind1;
	}
	if(side == 2){
		return ind2;
	}
	printf("ERROR: called getSide on contignode with side other than 1,2\n");
}

//Gets kmer for node1_p if side == 1, node2_p if side == 2
kmer_type Contig::getSideKmer(int side){
	if(side == 1){
		kmer_type kmer = getKmerFromRead(getSeq(), 0);
		if(ind1 == 4) return revcomp(kmer);
		return kmer;
	}
	if(side == 2){
		kmer_type kmer = getKmerFromRead(getSeq(), getSeq().length()-sizeKmer);
		if(ind2 == 4) return kmer;
		return revcomp(kmer);
	}
	printf("ERROR: tried to get a kmer corresponding to a side other than one or two from a contig.\n");
}

int Contig::getSide(ContigNode* node){
	if(node1_p == node){
		return 1;
	}
	if(node2_p == node){
		return 2;
	}
	printf("ERROR: tried to get the side of a contig node not adjacent to the contig.\n");
	std::cout << "Node1: " << node1_p << ", Node2: " << node2_p << " Input: " << node << "\n";
	return -1;
}

int Contig::getSide(ContigNode* node, int index){
	if((node1_p == node) && (ind1 == index)){
		return 1;
	}
	if((node2_p == node) && (ind2 == index)){
		return 2;
	}
	printf("ERROR: tried to get the side of a contig node,index pair, but didn't find it on either side.\n");
	std::cout << "Node1: " << node1_p << ", Node2: " << node2_p << " Input: " << node << "\n";
	return -1;
}

void Contig::setSide(int side, ContigNode* node){
	if(side == 1){
		node1_p = node;
	}
	else if(side == 2){
		node2_p = node;
	}
	else printf("ERROR: tried to set side for side other than 1,2.\n");	
}

bool Contig::isIsolated(){
	return ((node1_p == nullptr) && (node2_p == nullptr));
}

std::vector<std::pair<Contig*, bool>> Contig::getNeighbors(bool RC){
	if(!RC){ //forward node continuations 
	    if(node2_p){ //if node exists in forward direction 
	    	return node2_p->getFastGNeighbors(ind2);
		}
	}
	else{ //backward node continuations
		if(node1_p){ //if node exists in backward direction
			return node1_p->getFastGNeighbors(ind1);
		}
	}
	return {};
}

bool Contig::isDegenerateLoop(){
	return (node1_p == node2_p && ind1 == ind2);
}

bool Contig::checkValidity(){
	if(node1_p){
		if(node1_p->contigs[ind1] != this){
			printf("CONTIG_ERROR: adjacent node at specified index doesn't point back to this contig.\n");
			std::cout << "At " << getFastGName(true) << "\n";			
			return false;
		}
		if(getSide(node1_p, ind1) != 1 && !isDegenerateLoop()){
			printf("CONTIG_ERROR: getSide incorrect on node1p, ind1.\n");
			std::cout << "Node1: " << node1_p << ", Ind1: " << ind1 << ", Side: " << getSide(node1_p, ind1) << "\n";
			std::cout << "Node2: " << node2_p << ", Ind2: " << ind2 << ", Side: " << getSide(node2_p, ind2) << "\n";
			return false;
		}
	}
	if(node2_p){
		if(node2_p->contigs[ind2] != this){
			printf("CONTIG_ERROR: adjacent node at specified index doesn't point back to this contig.\n");
			std::cout << "At " << getFastGName(true) << "\n";
			return false;
		}
		if(getSide(node2_p, ind2) != 2 && !isDegenerateLoop()){
			printf("CONTIG_ERROR: getSide incorrect on node2p, ind2.\n");
			std::cout << "Node1: " << node1_p << ", Ind1: " << ind1 << ", Side: " << getSide(node1_p, ind1) << "\n";
			std::cout << "Node2: " << node2_p << ", Ind2: " << ind2 << ", Side: " << getSide(node2_p, ind2) << "\n";
			return false;
		}
	}
	return true;
}

string Contig::getFastGName(bool RC){
	stringstream stream;
    stream << "NODE_" << this << "_length_" << getSeq().length() << "_cov_" << getAvgCoverage();
    if(RC){
    	stream << "'";
    }
    return stream.str();
}

string Contig::getFastGHeader(bool RC){
	stringstream stream;
	stream << ">";
    stream << getFastGName(RC);

    //get neighbors in direction corresponding to RC value
    std::vector<std::pair<Contig*, bool>> neighbors = getNeighbors(RC);

    //if empty return now
    if(neighbors.empty()){
    	stream << ";" ;
    	return stream.str();
    }

    //not empty, add neighbors to line
    stream << ":";
    for(auto it = neighbors.begin(); it != neighbors.end(); it++){
    	Contig* neighbor = it->first;
    	bool RC = it->second;
    	stream << neighbor->getFastGName(RC) << ",";
    }
    string result = stream.str();
    result[result.length()-1] = ';';
    return result;
}

string Contig::getStringRep(){
	stringstream stream;
    stream << node1_p << "," << ind1 << " " << node2_p << "," << ind2 << "\n";
    stream << contigJuncs.getStringRep();
    stream << "\n";
    return stream.str();
}

Contig::Contig(){
	setSeq("");
	node1_p = nullptr;
	node2_p = nullptr;
	ind1 = -1;
	ind2 = -1;
	contigJuncs = ContigJuncList();
}

Contig::~Contig(){
	node1_p = nullptr;
	node2_p = nullptr;
}