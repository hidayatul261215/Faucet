#include "ContigGraph.h"
#include <math.h>
#include <time.h>
#include <stdlib.h>

unordered_map<kmer_type, ContigNode> *  ContigGraph::getNodeMap(){
    return &nodeMap;
}

//returns true if there are no two identical non-null nodes in the list
bool allDistinct(std::vector<ContigNode*> nodes){
    for(int i = 0; i < nodes.size(); i++){
        for(int j = i+1; j < nodes.size(); j++){
            if(nodes[i] == nodes[j] && nodes[i]){
                return false;
            }
        }
    }
    return true;
}

void ContigGraph::setReadLength(int length){
    read_length = length;
}

void ContigGraph::switchToNodeVector(){
    for(auto it = nodeMap.begin(); it != nodeMap.end(); ){
        kmer_type kmer = it->first;
        ContigNode* originalNode = &it->second;
        nodeVector.push_back(*originalNode);
        ContigNode* newNode = &nodeVector.back();
        for(int i = 0; i < 5; i++){
            if(newNode->contigs[i]){
                if(newNode->contigs[i]->node1_p == originalNode){
                    newNode->contigs[i]->node1_p = newNode;
                } 
                if(newNode->contigs[i]->node2_p == originalNode){
                    newNode->contigs[i]->node2_p = newNode;
                }
            }
        }
        it++;
        nodeMap.erase(kmer);
    }
}

bool ContigGraph::deleteTipsAndClean(){
    bool result = false;
    if(deleteTipsAndLowCoverageContigs() > 0){
        result = true;
    }   
    if(destroyDegenerateNodes() > 0){
        result = true;
    }
    if(collapseDummyNodes() > 0){
        result = true;
    }
    return result;
}

bool ContigGraph::breakPathsAndClean(Bloom* pair_filter){
    bool result = false;

    if(breakUnsupportedPaths(pair_filter) > 0){
        result = true;
    }
    if(destroyDegenerateNodes() > 0){
        result = true;
    }
    if(collapseDummyNodes() > 0){
        result = true;
    }

    return result;
}

bool ContigGraph::disentangleAndClean(Bloom* pair_filter){
    bool result = false;
    if(disentangle(pair_filter) > 0){
        result = true;
    }
    if(destroyDegenerateNodes() > 0){
        result = true;
    }
    if(collapseDummyNodes() > 0){
        result = true;
    }

    return result;
}

bool ContigGraph::cleanGraph(Bloom* short_pair_filter, Bloom* long_pair_filter){
    deleteTipsAndClean();

    bool result = false;
    if(breakPathsAndClean(short_pair_filter)){
        result = true;
    }
    if(disentangleAndClean(short_pair_filter)){
        result = true;
    }

    return result;
}

bool ContigGraph::checkGraph(){
    printf("Checking node mapped contigs.\n");
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        kmer_type kmer = it->first;
        ContigNode* node = &it->second;

        //since it uses the back contig to generate the kmer, can't run this check without one
        if(node->contigs[4]){
            if(kmer != node->getKmer()){
                printf("GRAPHERROR: node kmer didn't match nodemap kmer.\n");
                return false;
            }
        }

        //Want to run this check during cleaning, so can't look for this -it'll be violated till we destroy degenerate nodes
        // if(!node->contigs[4]){
        //     printf("GRAPHERROR: node lacks backcontig.\n");
        // }
        
        if(!node->checkValidity()){
            return false;
        }
        for(int i = 0; i < 5; i++){
            if(node->contigs[i]){
                if(!node->contigs[i]->checkValidity()){
                    return false;
                }
            }
        }                
    }

    printf("Checking %d isolated contigs.\n", isolated_contigs.size());
    //prints isolated contigs
    for(auto it = isolated_contigs.begin(); it != isolated_contigs.end(); it++){
        Contig* contig = &*it;
        if(contig->node1_p || contig->node2_p){
            printf("GRAPHERROR: isolated contig has pointer to at least one node.\n");
            return false;
        }  
    }

    printf("Done checking graph\n");
    return true;

}

//Returns a list of extensions which are there but have no support
std::vector<int> ContigGraph::getUnsupportedExtensions(ContigNode* node, Bloom* pair_filter){
    if(!node->contigs[4]){
        printf("GRAPHERROR: tried to test support at node with no backcontig.\n");
        return {};
    }
    int insertSize = 500;
    std::list<JuncResult> backResults = node->getPairCandidates(4, insertSize);
    std::list<JuncResult> forResults;
    std::vector<int> results = {};
    for (int i = 0; i < 4; i++){
        if(node->contigs[i]){
            Contig* contig = node->contigs[i];
            forResults = node->getPairCandidates(i, insertSize);
            if(getScore(backResults, forResults, pair_filter, .01)==0){
                results.push_back(i);
            }
        }   
    }
    return results;
}

bool ContigGraph::isTip(ContigNode* node, int index){
    Contig* contig = node->contigs[index];
    if(contig->getSeq().length() < read_length && contig->otherEndNode(node) == nullptr){
        return true;
    }
    return false;
}

bool ContigGraph::isLowCovContig(Contig* contig){
    if(contig->getAvgCoverage()<3){
        return true;
    }
    // if (20*contig->getAvgCoverage() < contig->getMinAdjacentCoverage()) { //more deletion for chimeras
    //     return true;
    // }   
    return false;
}

void ContigGraph::deleteContig(Contig* contig){
    //std::cout << contig << "\n";
    if(contig->node1_p){
        contig->node1_p->breakPath(contig->ind1);
    }
    if(contig->node2_p){
        contig->node2_p->breakPath(contig->ind2);
    }
    delete contig;
    contig = nullptr;
}


void ContigGraph::cutPath(ContigNode* node, int index){
    //std::cout << "Node: " << node << "\n";
    //std::cout << "Cov: \n" << node->getCoverage(0) << "\n";
    if(!node->contigs[index]){
        printf("ERROR: tried to cut nonexistant path.");
    }
    //printf("Did error check \n");
    Contig* contig = node->contigs[index];
    //printf("Getting side\n");
    int side = contig->getSide(node, index);
    int otherSide = 3 - side;
    //printf("A\n");
    if(contig->node1_p == contig->node2_p && contig->ind1 == contig->ind2){ //to handle hairpins
       // printf("A1\n");
        contig->setSide(side, nullptr); //set to point to null instead of the node
        contig->setSide(otherSide, nullptr);
    }
    else{
        //printf("A2\n");
        contig->setSide(side, nullptr);
    }
    //printf("To break path.\n");
    node->breakPath(index);
}



int ContigGraph::breakUnsupportedPaths(Bloom* pair_filter){
    printf("Breaking unsupported paths contigs. Starting with %d nodes. \n", nodeMap.size());
    int numBroken = 0;
    clock_t t = clock();
    //looks through all contigs adjacent to nodes
    int j = 1;
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        if(j % 1000 == 0){
            std::cout << j << " nodes processed. Time: "<< clock()-t << "\n";
            t = clock();
        }
        j++;
        ContigNode* node = &it->second;
        //printf("Got node.\n");
        std::vector<int> unsupported = getUnsupportedExtensions(node, pair_filter);
        for(auto it = unsupported.begin(); it != unsupported.end(); it++){
                    numBroken++;
                    // printf("Deleting contig %d \n" , numDeleted);
                    // std::cout << "Sequence: " << contig->seq << "\n";
                    // std::cout << "Header: " << contig->getFastGHeader(true) << "\n";
                    cutPath(node,*it);

                    // if(!checkGraph()){
                    //     std::cout << " Graph Check failed. Exiting.\n";
                    //     exit(1);
                    // }
                    // printGraph("lastValidGraph.fastg");
                    // printf("Deleted contig.\n");
                
            
        }
    }

    printf("Done breaking %d unsupported paths.\n", numBroken);
    return numBroken;
}   



int ContigGraph::deleteTipsAndLowCoverageContigs(){
    printf("Deleting error contigs. Starting with %d nodes. \n", nodeMap.size());
    int numDeleted = 0;

    printf("Deleting node mapped contigs.\n");
    //looks through all contigs adjacent to nodes
    int j = 1;
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        if(j % 10000 == 0){
            std::cout << j << " nodes processed.\n";
        }
        j++;
        ContigNode* node = &it->second;
        //printf("Got node.\n");
        for(int i = 0; i < 5; i++){
            Contig* contig = node->contigs[i];
            if(contig){
                //printf("Checking contig %d.\n", i);
                if(isLowCovContig(contig) || isTip(node, i)){
                    numDeleted++;
                    // printf("Deleting contig %d \n" , numDeleted);
                    // std::cout << "Sequence: " << contig->seq << "\n";
                    // std::cout << "Header: " << contig->getFastGHeader(true) << "\n";
                    deleteContig(contig);

                    // if(!checkGraph()){
                    //     std::cout << " Graph Check failed. Exiting.\n";
                    //     exit(1);
                    // }
                    // printGraph("lastValidGraph.fastg");
                    //printf("Deleted contig.\n");
                }
            }
        }
    }

    printf("Done deleting node mapped contigs.\n");
    printf("Deleting isolated contigs. Starting with %d.\n", isolated_contigs.size());
    //prints isolated contigs
    for(auto it = isolated_contigs.begin(); it != isolated_contigs.end();){
        Contig* contig = &*it;
        if(isLowCovContig(contig)){
            numDeleted++;
            it = isolated_contigs.erase(it);
        }
        else it++;
    }

    printf("Done deleting %d error contigs.\n", numDeleted);
    return numDeleted;
}   

int ContigGraph::destroyDegenerateNodes(){
    printf("Destroying degenerate nodes. Starting with %d nodes.\n", nodeMap.size());
    int numDegen = 0;

    //looks through all contigs adjacent to nodes
    for(auto it = nodeMap.begin(); it != nodeMap.end(); ){
        ContigNode* node = &it->second;
        kmer_type kmer = it->first;
        it++;
        if(node->numPathsOut() == 0){
            if(node->contigs[4]){
                cutPath(node, 4);
            }
            numDegen++;
            nodeMap.erase(kmer);
        }
        else if(!node->contigs[4]){
            for(int i = 0; i < 4; i++){
                if(node->contigs[i]){
                    cutPath(node,i);
                }
            }
            numDegen++;
            nodeMap.erase(kmer);
        }
    }

    printf("Done destroying %d nodes.\n", numDegen);
    return numDegen;
}

double ContigGraph::getTailBound(int numTrials, double p, int result){
    double mean = 1.0*numTrials*p;
    double delta = (1.0*result / mean) - 1;
    double chernoffBound;
    if( delta < 0){
        chernoffBound = 1.0;
    }
    else if(delta < 1){
        chernoffBound = exp(-delta*delta*mean/3);
    }
    else if (delta > 1){
        chernoffBound = exp(-delta*mean/3);
    }
    //std::cout <<" Chernoff: " << chernoffBound << " \n";
    double markovBound = 1.0;
    if (result >  mean){
        markovBound = mean/result;
    }
    //std::cout <<" Markov: " << markovBound << " \n";
    //std::cout << "Min: " << std::min(chernoffBound, markovBound) << "\n";
    return std::min(chernoffBound, markovBound);
}

//returns a score based on how many pairs of kmers from the first and second lists are in the filter,
//relative to the FP rate of the filter
double ContigGraph::getScore(std::list<JuncResult> leftCand, std::list<JuncResult> rightCand, Bloom* pair_filter, double fpRate){
    double score = 0;
    int insertSize = 500;
    int readLength = 100;
    int counter = 0;

    //Looks for junction pairs from a single read, within readlength of the junction either way
    for(auto itL = leftCand.begin(); itL != leftCand.end() && itL->distance < readLength; itL++){
        for(auto itR = rightCand.begin(); itR != rightCand.end()  && itR->distance < readLength; itR++){
            counter++;
            if(pair_filter->containsPair(JuncPair(itL->kmer, itR->kmer))){
                //std::cout << "Distance: " << itL->distance + itR->distance << "\n";
                score += 1;
            } 
        }
    }

    //Looks for junction pairs one full insert size apart
    //Only searches for pairs at distances [IS-readLength, IS+readLength]
    //TODO: come up with way of gauging variance in IS and redefine this based on that
    std::reverse(leftCand.begin(), leftCand.end());
    auto rightStart = rightCand.begin();
    for(auto itL = leftCand.begin(); itL != leftCand.end(); itL++){
        while(rightStart != rightCand.end() && rightStart->distance + itL->distance < insertSize - readLength){
            rightStart++;
        }
        if(rightStart == rightCand.end()) {break;}
        for(auto itR = rightStart; itR != rightCand.end() 
            && itR->distance + itL->distance < insertSize + readLength; itR++){
            counter++;
            if(pair_filter->containsPair(JuncPair(itL->kmer, itR->kmer))){
                //std::cout << "Distance: " << itL->distance + itR->distance << "\n";
                score += 1;
            } 
        }
    }

    //std::cout << "Total pairs: " << rightCand.size()*leftCand.size() << ", counter: " << counter << "\n";
    //std::cout << "Junctions: " << leftCand.size() << "," << rightCand.size() << ". Score: " << score << "\n";
    return score; //getTailBound(leftCand.size()*rightCand.size(),fpRate, score);
}

int ContigGraph::disentangle(Bloom* pair_filter){
    int disentangled = 0;
    double fpRate = pow(pair_filter->weight(), pair_filter->getNumHash());

    std::cout << "About to disentangle.  Starting with " << nodeMap.size() << " nodes.\n";
    //looks through all contigs adjacent to nodes
    for(auto it = nodeMap.begin(); it != nodeMap.end(); ){
        //printf("-1\n");
        ContigNode* node = &it->second;
        //printf("Testing %s\n", print_kmer(node->getKmer()));
        kmer_type kmer = it->first;
        //printf("0\n");
        if(node->numPathsOut() == 2){
            //printf("1\n");
            Contig* contig = node->contigs[4];
            ContigNode* backNode = contig->otherEndNode(node);
            //printf("2\n");
            if(backNode && node != backNode && backNode->numPathsOut() == 2 && backNode->indexOf(contig) == 4){
                //printf("3\n");
                // printf("Found an outward facing pair: %s,", print_kmer(node->getKmer()));
                // printf("%s\n", print_kmer(backNode->getKmer()));
                int a,b,c,d;
                a = backNode->getIndicesOut()[0], b = backNode->getIndicesOut()[1];
                c = node->getIndicesOut()[0], d = node->getIndicesOut()[1];
                int insertSize = 500;
                std::list<JuncResult> A = backNode->getPairCandidates(a, insertSize);
                std::list<JuncResult> B = backNode->getPairCandidates(b, insertSize);
                std::list<JuncResult> C = node->getPairCandidates(c, insertSize);
                std::list<JuncResult> D = node->getPairCandidates(d, insertSize);
              
                double scoreAC = getScore(A,C, pair_filter, fpRate);
                double scoreAD = getScore(A,D, pair_filter, fpRate);
                double scoreBC = getScore(B,C, pair_filter, fpRate);
                double scoreBD = getScore(B,D, pair_filter, fpRate);
                //std::cout << "Score: " << scoreAC << "," << scoreBD << "," << scoreAD << "," << scoreBC << "\n";
                //if(scoreAC <.05 && scoreBD < .05 && scoreAD > .3 && scoreBC > .3){
                if(std::min(scoreAC,scoreBD) > 0 && std::max(scoreAD,scoreBC) == 0){
                    //printf("Found pair in filter in first orientation.\n");
                    
                    if(disentanglePair(contig, backNode, node, a, b, c, d)){
                        //printf("Disentangled pair.\n");
                        it = nodeMap.erase(it);
                        //printf("1\n");
                        if(it != nodeMap.end()){
                            //printf("2\n");
                            if(backNode->getKmer() == it->first){
                                //printf("3\n");
                                it++;
                            }
                        }
                        //printf("4\n");
                        nodeMap.erase(backNode->getKmer());
                        //printf("5\n");
                        disentangled++;
                        //printf("6\n");
                        continue;
                    }
                }
                if(std::min(scoreAD , scoreBC) > 0 && std::max(scoreAC , scoreBD) == 0){
                //if(scoreAD <.05 && scoreBC < .05 && scoreAC > .3 && scoreBD > .3){
                    //printf("Found pair in filer in second orientation\n");
                    if(disentanglePair(contig, backNode, node, a,b,d,c)){
                        //printf("Disentangled pair.\n");
                        it = nodeMap.erase(it);
                        if(it != nodeMap.end()){
                            if(backNode->getKmer() == it->first){
                                it++;
                            }
                        }
                        nodeMap.erase(backNode->getKmer());
                        disentangled++;
                        continue;
                    } 
                }
            }
       }
       it++;
    }

    printf("Done disentangling %d pairs of nodes.\n", disentangled);
    return disentangled;
}

//a,b are on backNode, c,d are on forwardNode
//a pairs with c, b pairs with d
/*
       a\                       /c              a--------c
         --------contig---------      ---->
       b/                       \d              b--------d
*/
bool ContigGraph::disentanglePair(Contig* contig, ContigNode* backNode, ContigNode* forwardNode, 
    int a, int b, int c, int d){
    Contig* contigA = backNode->contigs[a];
    Contig* contigB = backNode->contigs[b];
    Contig* contigC = forwardNode->contigs[c];
    Contig* contigD = forwardNode->contigs[d];

    ContigNode* nodeA = contigA->otherEndNode(backNode);
    ContigNode* nodeB = contigB->otherEndNode(backNode);
    ContigNode* nodeC = contigC->otherEndNode(forwardNode);
    ContigNode* nodeD = contigD->otherEndNode(forwardNode);

    if(!allDistinct({backNode, forwardNode, nodeA, nodeB, nodeC, nodeD})){
        return false;
    }
    Contig* contigAC = contigA->concatenate(contig, contigA->getSide(backNode), contig->getSide(backNode));
    contigAC = contigAC->concatenate(contigC, contigAC->getSide(forwardNode), contigC->getSide(forwardNode));
    if(nodeA){
        nodeA->replaceContig(contigA, contigAC);
    }
    if(nodeC){
        nodeC->replaceContig(contigC, contigAC);
    }
    if(!nodeA && !nodeC){
        isolated_contigs.push_back(*contigAC);
    }

    Contig* contigBD = contigB->concatenate(contig, contigB->getSide(backNode), contig->getSide(backNode));
    contigBD = contigBD->concatenate(contigD, contigBD->getSide(forwardNode), contigD->getSide(forwardNode));
    if(nodeB){
        nodeB->replaceContig(contigB, contigBD);
    }
    if(nodeD){
        nodeD->replaceContig(contigD, contigBD);
    }
    if(!nodeB && !nodeD){
        isolated_contigs.push_back(*contigBD);
    }
    return true;
}
    
int ContigGraph::collapseDummyNodes(){
   printf("Collapsing dummy nodes.\n");
    int numCollapsed = 0;

    //looks through all contigs adjacent to nodes
    for(auto it = nodeMap.begin(); it != nodeMap.end(); ){
        ContigNode* node = &it->second;
        kmer_type kmer = it->first;
        it++;
        if(node->numPathsOut() == 1){
            numCollapsed++;
            collapseNode(node, kmer);
        }
    }

    printf("Done collapsing %d nodes.\n", numCollapsed);
    return numCollapsed;
}

void ContigGraph::collapseNode(ContigNode * node, kmer_type kmer){
    Contig* backContig = node->contigs[4];
    Contig* frontContig;
    if(!backContig){
        printf("WTF no back\n");
    }
    int fronti = 0;
    for(int i = 0; i < 4; i++){
        if(node->contigs[i]) {
            frontContig = node->contigs[i];
            fronti = i;
        }
    }
    if(!frontContig){
        printf("WTF no front\n");
    }
    if(frontContig == backContig){ //circular sequence with a redundant node
        frontContig->node1_p=nullptr;
        frontContig->node2_p=nullptr;
        addIsolatedContig(*frontContig);
        delete backContig;
    }
    else { //normal case of collapsing a node between two other nodes
        ContigNode* backNode = backContig->otherEndNode(node);
        ContigNode* frontNode = frontContig->otherEndNode(node);


        int backSide = backContig->getSide(node, 4);
        int frontSide = frontContig->getSide(node, fronti);

        Contig* newContig = backContig->concatenate(frontContig, backSide, frontSide);
        if(backNode){
               backNode->contigs[newContig->ind1] = newContig;
        }
        if(frontNode){
            frontNode->contigs[newContig->ind2] = newContig;
        }
        if(!backNode && !frontNode){
            addIsolatedContig(*newContig);
            delete newContig;
        } 
        delete backContig;
        delete frontContig;
    }
    if(!nodeMap.erase(kmer)) printf("ERROR: tried to erase node %s but there was no node.\n", print_kmer(kmer));
}


void ContigGraph::printGraph(string fileName){
    printf("Printing graph from contig graph to fastg with iterator.\n");
    ofstream fastgFile;
    fastgFile.open(fileName);

    ContigIterator* contigIt = new ContigIterator(this);

    //prints contigs that are adjacent to nodes
    while(contigIt->hasNextContig()){
        Contig* contig = contigIt->getContig();
        printContigFastG(&fastgFile, contig);
    }

    //prints isolated contigs
    for(auto it = isolated_contigs.begin(); it != isolated_contigs.end(); it++){
        Contig contig = *it;
        printContigFastG(&fastgFile, &contig);
    }

    //printf("Done printing contigs from contig graph.\n");
    fastgFile.close();
    printf("Done printing graph from contig graph iterator.\n");
}

void ContigGraph::printContigFastG(ofstream* fastgFile, Contig * contig){
    *fastgFile << contig->getFastGHeader(true) << "\n";
    *fastgFile << contig->getSeq() << "\n";
    *fastgFile << contig->getFastGHeader(false) << "\n";
    *fastgFile << contig->getSeq() << "\n";
}

void ContigGraph::addIsolatedContig(Contig contig){
    isolated_contigs.push_back(contig);
}

void ContigGraph::printContigs(string fileName){
    printf("Printing contigs from contig graph.\n");
    ofstream jFile;
    jFile.open(fileName);
    int lineNum = 1;
    //printf("Printing contigs from contig graph of %d nodes.\n", nodeMap.size());

    //prints contigs that are adjacent to nodes
    for(auto it = nodeMap.begin(); it != nodeMap.end(); it++){
        ContigNode* node = &it->second;
        for(int i = 0; i < 5; i++){
            if(node->contigs[i]){
                Contig* contig = node->contigs[i];
                if(contig->getSide(node,i) == 1){
                    //printf("Printing from node at index %d\n", i);
                    // std::cout << "Contig " << lineNum << ":\n";
                    // std::cout << contig->seq << "\n";
                    jFile << canon_contig(contig->getSeq() ) << "\n";
                }
            }
        }
    }

    //prints isolated contigs
    for(auto it = isolated_contigs.begin(); it != isolated_contigs.end(); it++){
        Contig contig = *it;
        //printf("Printing isolated contig.\n");
        jFile << canon_contig(contig.getSeq()) << "\n";
    }

    //printf("Done printing contigs from contig graph.\n");
    jFile.close();
    printf("Done printing contigs from contig graph.\n");
}

ContigGraph::ContigGraph(){
    nodeMap = {};
    isolated_contigs = {};
    nodeVector = {};
}

//Creates a contig node and returns it or returns the already extant one if it's already extant
//Uses coverage info from junction to create the node
ContigNode * ContigGraph::createContigNode(kmer_type kmer, Junction junc){
    return &(nodeMap.insert(std::pair<kmer_type, ContigNode>(kmer, ContigNode(junc))).first->second);
}

Contig* ContigGraph::getLongestContig(){
    ContigIterator* contigIt = new ContigIterator(this);
    Contig* result;
    int maxLength = 0;
    while(contigIt->hasNextContig()){
        Contig* contig = contigIt->getContig();
        int length = contig->length();
        if(length > maxLength){
            result = contig;
            maxLength = length;
        }
    }
    return result;
}