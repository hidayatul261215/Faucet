#include "Graph.h"
#include <time.h>
#include <fstream>
#include <iostream> 
#include <sstream>

using std::ofstream;
using std::string;
using std::stringbuf;

Graph::Graph(Bloom* bloo1, JChecker* jcheck){
    bloom = bloo1;
    jchecker = jcheck;
    nodeMap = {};
    realExtensions = {};
    sinks = {};
}

//returns the index from startNode that leads to the destination kmer
int Graph::getPathIndex(Node startNode, kmer_type destinationKmer){
    int index = -1;
    for(int i = 0; i < 5; i++){
        if(startNode.nextJunc[i] == destinationKmer){
            return i;
        }
    }
}

Node * Graph::getNode(kmer_type kmer){
    return &(nodeMap.find(kmer)->second);
}

bool Graph::isSink(kmer_type kmer){
    return sinks->find(kmer) != sinks->end();
}

bool Graph::isNode(kmer_type kmer){
    return nodeMap.find(kmer) != nodeMap.end();
}

bool Graph::isRealExtension(kmer_type kmer, int ext){
    auto it = realExtensions->find(kmer);
    if(it == realExtensions->end()){
        return false;
    }
    return it->second == ext;
}

//Gets the valid extension of the given kmer based on the bloom filter and cFPs.  JChecks! so this cuts off tips
//Assume its not a junction
//Returns -1 if there is no valid extension
//Assumes full cFP set and uses it as reference
int Graph::getValidJExtension(DoubleKmer kmer, int dist, int max){
    //printf("Getting valid extension of %s\n", print_kmer(kmer.kmer));
    kmer_type nextKmer;
    int answer = -1;
    for(int i = 0; i < 4; i++){
        nextKmer = kmer.getExtension(i, FORWARD);
        if(bloom->oldContains(get_canon(nextKmer))){
            if(jchecker->jcheck(nextKmer)){
               
                if (answer == -1 ){ //if this is the first correct option found
                    answer = i; 
                } 
                else { //second correct answer! check real extensions
                    if(isRealExtension(kmer.kmer, i)){
                        return i;
                    }
                            //REMOVE AFTER TESTING!
                            auto it = realExtensions->find(kmer.kmer);
                            if(it == realExtensions->end()){
                                printf("Base kmer %s at %d/%d is not in the real extension map.\n", print_kmer(kmer.kmer), dist, max);
                                printf("Real extensions: %d, %d\n", answer, i);
                            }   
                }
            }
        }
    }
    return answer;
}

//Assumes cFPs and sinks were already handled, only complex junctions should remain.  
//Gets junctions from juncMap and builds equivalent Nodes for graph. Kills junctions as it goes.
void Graph::getNodesFromJunctions(JunctionMap* juncMap){
    kmer_type kmer;
    Junction junction;
    Node node;
    for(auto it = juncMap->junctionMap.begin(); it != juncMap->junctionMap.end(); ){
        kmer = it->first;
        junction = it->second;
        it++; //must update iterator before killing the kmer
        if(junction.numPathsOut()>1){
            node = Node(junction);
            nodeMap.insert(std::pair<kmer_type, Node>(kmer, node));
            juncMap->killJunction(kmer);
        }
        else{
            fprintf(stderr, "ERROR: found junction with one or no paths out.\n");
        }
    }
}

//Simply replaces all of the junctions of juncMap with nodes in the graph.  After this the juncMap
//Should be empty and cFPs, sinks, and nodes should exist everywhere they need to.
void Graph::buildGraph(JunctionMap* juncMap){
    //get sinks and cFPs
    time_t start, stop;

    time(&start);
    sinks = juncMap->getSinks();
    time(&stop);

    printf("Sinks found: %d\n", sinks->size());
    printf("Time to find sinks: %f\n", difftime(stop,start));

    time(&start);
    realExtensions = juncMap->getRealExtensions();
    time(&stop);

    printf("Real extensions found: %d\n", realExtensions->size());
    printf("Time to find real extensions: %f\n", difftime(stop,start));

    time(&start);
    getNodesFromJunctions(juncMap);
    time(&stop);
    printf("Time to get nodes from junctions: %f\n", difftime(stop,start));
}

//puts in distances and junction IDs to link the two nodes
void Graph::directLinkNodes(kmer_type kmer1, int index1, kmer_type kmer2, int index2, int distance){
    Node* node1 = getNode(kmer1);
    Node* node2 = getNode(kmer2); 
    node1->update(index1, distance, kmer2);
    node2->update(index2, distance, kmer1);
}

//puts in distances and junction IDs to link the node to the sink
void Graph::linkNodeToSink(kmer_type nodeKmer, int index, kmer_type sinkKmer, int distance){
    Node* node = getNode(nodeKmer);
    node->update(index, distance, sinkKmer);
}

void Graph::linkNeighbor(Node node, kmer_type startKmer, int index, BfSearchResult result){
    if(result.isNode){
        directLinkNodes(startKmer, index, result.kmer, result.index, result.distance);
    }
    else{
        linkNodeToSink(startKmer, index, result.kmer, result.kmer);
    }
}

//Finds the neighbor of the given node on the given extension using the graph.
GraphSearchResult Graph::findNeighborGraph(Node node, kmer_type startKmer, int index){
    kmer_type kmer = node.nextJunc[index];
    bool isNode = nodeMap.find(kmer) != nodeMap.end();
    int otherIndex = -1;
    if(isNode){
        otherIndex = getPathIndex(nodeMap.find(kmer)->second, startKmer);
    }
    int distance = node.dist[index];
    return GraphSearchResult { kmer, isNode, otherIndex, distance };
}

//Finds the neighbor of the given node on the given extension.  
BfSearchResult Graph::findNeighborBf(Node node, kmer_type startKmer, int index){
    DoubleKmer doubleKmer(startKmer);
    int dist = 1;
    int lastNuc;//stores the last nuc so we know which extension we came from
    stringbuf contig;

    //First, get to the first kmer from which we can do a proper bloom scan.  This is different for forwards and backward extensions
    if(index == 4){
        //in this case that's the reverse kmer 
        doubleKmer.reverse(); 
        for(int i = 0; i < sizeKmer; i++){
            contig.sputc(getNucChar(code2nucleotide(doubleKmer.kmer, i)));
        }
        if(isNode(doubleKmer.kmer)){
            return BfSearchResult { doubleKmer.kmer, true, 4, 1, contig.str() };
        }
        if(isSink(doubleKmer.kmer)){
            return BfSearchResult { doubleKmer.kmer, false, -1, 1, contig.str() } ;
        }
        dist = 2;//set it up for second position scan below
    }
    else{
        //in this case thats the next forward kmer- but since we're at a junction we must get there manually using the given index, no bloom scan possible 
        lastNuc =first_nucleotide(doubleKmer.revcompKmer); 
        doubleKmer.forward(index);
        for(int i = 0; i < sizeKmer; i++){
            contig.sputc(getNucChar(code2nucleotide(doubleKmer.kmer, i)));
        }
        if(isNode(doubleKmer.revcompKmer)){
            return BfSearchResult { doubleKmer.revcompKmer, true, lastNuc, 1, contig.str() };
        }
        dist = 2;
        if(isNode(doubleKmer.kmer)){
            directLinkNodes(startKmer, index, doubleKmer.kmer, 4, 2);
            return BfSearchResult { doubleKmer.kmer, true, 4, 2, contig.str() };
        }
        if(isSink(doubleKmer.kmer)){
            return BfSearchResult { doubleKmer.kmer, false, -1, 2, contig.str() };
        }
        dist = 3;//set it up for third position scan below
    }

    //Now, bloom scan forward until there's no chance of finding a junction that indicates an overlapping kmer 
    while(true){ 
        //move forward if possible
        int validExtension = getValidJExtension(doubleKmer, dist,getNode(startKmer)->dist[index] );
        if(validExtension == -1){
            //Note: tested, never at a sink
            printf("ERROR: No valid extension of %s while searching from ",  print_kmer(doubleKmer.kmer));
            printf("%s on index %d at dist %d/%d\n",  print_kmer(startKmer), index, dist, getNode(startKmer)->dist[index]);
            contig.sputn("SINKERROR", 9);
            return BfSearchResult { -1, false, -1, -1, contig.str() };
            //should not happen since we checked for sinks already
        }
        lastNuc = first_nucleotide(doubleKmer.revcompKmer);
        doubleKmer.forward(validExtension); 

        //handle backward junction case
        if(isNode(doubleKmer.revcompKmer)){
            return BfSearchResult { doubleKmer.revcompKmer, true, lastNuc, dist, contig.str() };
        }
        dist++;

        //this happens in the middle of rev and forward directions since we don't want to include 
        //this nuc in the contig if the other junction is facing backward
        contig.sputc(getNucChar(validExtension));

        //handle forward junction case
        if(isNode(doubleKmer.kmer)){
            return BfSearchResult { doubleKmer.kmer, true, 4, dist, contig.str() };
        }
        if(isSink(doubleKmer.kmer)){
            return BfSearchResult { doubleKmer.kmer, false, -1, dist, contig.str() };
        }
        dist++;
    }
}

//private utility for doing a BF traversal and either printing contigs or linking nodes or both
void Graph::traverseContigs(bool linkNodes, bool printContigs){
    kmer_type kmer;
    Node node;
    BfSearchResult result;
    ofstream cFile(contigFile);
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        kmer = it->first;
        node = it->second;
        for(int i = 0; i < 5; i++){
            if(node.cov[i]  > 0 || i == 4){ //if there is coverage but not yet a link
                result = findNeighborBf(node, kmer, i);
                if(linkNodes && node.nextJunc[i] == -1){
                    linkNeighbor(node, kmer, i, result);
                }
                if(printContigs){
                    cFile << result.contig << "\n";
                }
            }
        }
    }
    cFile.close();
}

//Assumes the graph has all nodes, sinks, and cFPs properly initialized. 
//Iterates through the nodes and prints every contig path
void Graph::printContigs(string filename){
    contigFile = filename;
    time_t start,stop;
    time(&start);
    printf("Printing contigs.\n");

    traverseContigs(false, true);
    
    printf("Done printing contigs.\n");
    time(&stop);
    printf("Time: %f\n", difftime(stop, start));
}

//Assumes the graph has all nodes, sinks, and cFPs properly initialized. 
//Iterates through the nodes and links all adjacent nodes and sinks.
void Graph::linkNodes(){
    time_t start,stop;
    time(&start);
    printf("Linking nodes.\n");

    traverseContigs(true, false);

    printf("Done linking nodes.\n");
    time(&stop);
    printf("Time: %f\n", difftime(stop, start));
}

bool Graph::isTip(Node node, int index, int maxTipLength){
    return sinks->find(node.nextJunc[index]) != sinks->end() && node.dist[index] <= maxTipLength;
}

int Graph::getNumTips(Node node, int maxTipLength){
    int numTips = 0;
    for(int i = 0; i < 4; i++){
        if(isTip(node, i, maxTipLength)){
            numTips++;
        }
    }
    return numTips;
}

//Deletes a node that only has one real extension, replacing it with an entry in the realExtension map
//Does not link bridging nodes, that must be done beforehand.
void Graph::deleteNode(kmer_type kmer){ 
    if(nodeMap.find(kmer) == nodeMap.end()){
        printf("ERROR: tried to delete nonexistant node.\n");
    }
    Node node = nodeMap.find(kmer)->second;
    int numPaths = 0;
    int realPath = -1;
    for(int i = 0; i < 4; i++){
        if(node.cov[i] > 0){
            numPaths++;
            realPath = i;
        }
    }
    if(numPaths != 1){
        printf("ERROR: Tried to delete a node that did not have exactly one real path.\n");
    }
    (*realExtensions)[kmer] = realPath;
    nodeMap.erase(kmer);
}

void Graph::cutTips(int maxTipLength){
    time_t start,stop;
    time(&start);
    printf("Cutting tips.\n");

    int numTipsCut = 0, numNodesRemoved = 0;
    kmer_type kmer;
    Node* node;
    for(auto it = nodeMap.begin(); it != nodeMap.end(); ){
        kmer = it->first;
        node = &it->second;

        int numTips = getNumTips(*node, maxTipLength);
        if(numTips > 0 && numTips < node->numPathsOut()){
            for(int i = 0; i < 4; i++){
                if(isTip(*node, i, maxTipLength)){
                    if(sinks->find(node->nextJunc[i]) == sinks->end()){
                        printf("ERROR: trimmed a tip but it didn't lead to a sink. \n");
                        printf("Supposed sink kmer %s\n", print_kmer(node->nextJunc[i]));
                    }
                    sinks->erase(node->nextJunc[i]); 
                    node->deletePath(i);
                    numTipsCut++;
                }
            }

            if(node->numPathsOut() == 1){
                GraphSearchResult last, next;
                for(int i = 0; i < 4; i++){
                    if(node->cov[i] > 0){
                        next = findNeighborGraph(*node, kmer, i);
                        if(!next.isNode){
                            printf("ERROR: only remaining path doesn't point to a node.");
                        }
                    }
                }
                last = findNeighborGraph(*node, kmer, 4);

                if(last.isNode){ //back kmer is a node!
                    directLinkNodes(last.kmer, last.index, next.kmer, next.index, last.distance + next.distance);
                }
                else{
                    linkNodeToSink(next.kmer, next.index, last.kmer, last.distance + next.distance);
                }
                deleteNode(kmer);
                numNodesRemoved++;
                it++;
                continue;
            }
        }
        it++;
    }
    printf("Done cutting %d tips and removing %d nodes.\n", numTipsCut, numNodesRemoved);
    time(&stop);
    printf("Time: %f\n", difftime(stop, start));
}

void Graph::printGraph(string fileName){
    kmer_type kmer;
    Node node;
    int ext;
    ofstream jFile;
    jFile.open(fileName);
    printf("Writing graph to file.\n");
    printf("Writing graph nodes to file.\n");
    jFile << "Graph nodes: \n";
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        kmer = it->first;
        node = it->second;
        jFile << print_kmer(kmer) << " ";
        node.writeToFile(&jFile);
        jFile << "\n";
    }
    printf("Done writing graph nodes to file \n");

    printf("Writing sinks to file.\n");
    jFile <<"Sinks: \n";
    for(auto it = sinks->begin(); it != sinks->end(); it++){
        kmer = *it;
        jFile << print_kmer(kmer) << "\n";
    }
    printf("Done writing sinks to file.\n");

    printf("Writing real extensions to file.\n");
    jFile <<"Real extensions: \n";
    for(auto it = realExtensions->begin(); it != realExtensions->end(); it++){
        kmer = it->first;
        ext = it->second;
        jFile << print_kmer(kmer) << " " << getNucChar(ext) << "\n";
    }
    printf("Done writing real extensions to file.\n");
    printf("Done writing graph to file.\n");
    jFile.close();
}