#ifndef TURBOISO_CPP
#define TURBOISO_CPP

#include <iostream>
#include <queue>        // std::priority_queue
#include <boost/foreach.hpp>
#include <unordered_set>

#include "parsefs.cpp"
#include "graph.cpp"
#include "defines.h"
#include "turboiso.h"

using namespace std;
bool ExploreCandidateRegions(DataGraph& dg, NECNode* nec_root,
                             vector<VERTEX>* data_node_candidates,
                             unordered_set<VERTEX>* marked_vertex, 
                             CandidateRegions* cr,
                             VERTEX parent);

QueryNode* GetStartingQueryVertex(DataGraph& dg, query_node_map* qg) {
    QueryNode* tmp;
    RankingPair* rp;
    priority_queue<RankingPair*, vector<RankingPair*>, CompareRankingPair> pq;

    /*
     * TODO : When the filters get implemented, refactor this
     */
    for(int index = 0; index < qg->size(); index += 1) {
        tmp = qg->at(index);
        double score_denominator = tmp->parents->size() + tmp->children->size();
        double score_numerator = dg.vfs->NumVertices(tmp->label);

        rp = new RankingPair;
        rp->v = tmp;
        rp->score = score_numerator / score_denominator;

        // Adjust PQ, keeping within fixed candidate size
        if(pq.size() < STARTING_VERTEX_CANDIDATE_SIZE) {
            pq.push(rp);
        } else {
            if(pq.top()->score > rp->score) {
                // TODO : Fix memory leak. Pop does not free the allocated
                // RankingPair
                delete pq.top();
                pq.pop();
                pq.push(rp);
            } else {
                delete rp;
            }
        }
    }
    tmp = pq.top()->v;
    while(pq.size() > 0) {
        rp = pq.top();
        pq.pop();
        delete rp;
    }

    /*
     * TODO : Two filters and CR estimation later
     */
    return tmp;
}

void GetSortedNeighborLabels(vector<QueryNode*>* nodes, vector<LABEL>* labels) {
    QueryNode* adj_node;
    for(int i = 0; i < nodes->size(); i += 1) {
        adj_node = nodes->at(i);
        labels->push_back(adj_node->label);
    }
    // Sort Ascending. Order doesn't actually matter.
    sort(labels->begin(), labels->end());
}

vector<NECNode*>* MatchNECNodes(NECBucketMap* node_buckets) {
    vector<NECNode*>* nec_nodes;
    vector<QueryNode*>* query_nodes;
    vector<LABEL> nec_child_labels;
    vector<LABEL> nec_parent_labels;
    vector<LABEL> cmp_child_labels;
    vector<LABEL> cmp_parent_labels;
    QueryNode* origin_node;
    QueryNode* cmp_node;
    NECNode* nec_node;

    nec_nodes = new vector<NECNode*>;
    for(NECBucketMap::iterator it = node_buckets->begin(); 
        it != node_buckets->end(); ++it) {
        query_nodes = it->second;

        while(query_nodes->size() > 0) {
            origin_node = query_nodes->back();
            query_nodes->pop_back();
            nec_node = NECNodeInit(); // Allocates memory for member variables as well
            nec_node->members->push_back(origin_node);
            nec_nodes->push_back(nec_node);

            // Get adjacent information
            // This is the slow path. Take this after the hash path :
            // TODO : two separate hashes for parent and child labels
            GetSortedNeighborLabels(origin_node->children, &nec_child_labels);
            GetSortedNeighborLabels(origin_node->parents, &nec_parent_labels);

            // Iterate over all remaining nodes in vector (in reverse) to find
            for(int node_index = query_nodes->size() - 1;
                node_index >= 0; node_index -= 1) {
                cmp_node = query_nodes->at(node_index);
                GetSortedNeighborLabels(cmp_node->children, &cmp_child_labels);
                GetSortedNeighborLabels(cmp_node->parents, &cmp_parent_labels);
                if (cmp_child_labels == nec_child_labels &&
                    cmp_parent_labels == nec_parent_labels) {
                    query_nodes->erase(query_nodes->begin() + node_index);
                    nec_node->members->push_back(cmp_node);
                }
                cmp_parent_labels.clear();
                cmp_child_labels.clear();
            }
            // same adjacent signatures
            nec_child_labels.clear();
            nec_parent_labels.clear();
        }
    }

    return nec_nodes;
}

vector<NECNode*>* GenerateNECNodes(unordered_set<QueryNode*>* pnodes,
                                   unordered_set<QueryNode*>* seen)
{
    vector<QueryNode*>* bucket_vector;
    NECBucketMap query_node_buckets;
    QueryNode* curr_node;
    
    unordered_set<QueryNode*>::iterator it = pnodes->begin();
    for(; it != pnodes->end(); ++it) {
        curr_node = *(it);

        // Check if node has been marked.
        if(seen->count(curr_node) == 1) {
            continue;
        }
        // Mark node
        seen->insert(curr_node);
        if (query_node_buckets.count(curr_node->label) == 0) {
            query_node_buckets.insert(
                make_pair<LABEL,vector<QueryNode*>*>(curr_node->label,new vector<QueryNode*>));
        }
        bucket_vector = query_node_buckets[curr_node->label];
        bucket_vector->push_back(curr_node);
    }

    return MatchNECNodes(&query_node_buckets);
}

unordered_set<QueryNode*>* GetAdjacentChildQueryNodes(NECNode* n) {
    QueryNode* curr;
    unordered_set<QueryNode*>* s = new unordered_set<QueryNode*>;
    for (int member = 0; member < n->members->size(); member += 1) {
        curr = n->members->at(member);
        for(int child = 0; child < curr->children->size(); child += 1) {
            s->insert(curr->children->at(child));
        }
    }
    return s;
}

unordered_set<QueryNode*>* GetAdjacentParentQueryNodes(NECNode* n) {
    QueryNode* curr;
    unordered_set<QueryNode*>* s = new unordered_set<QueryNode*>;
    for (int member = 0; member < n->members->size(); member += 1) {
        curr = n->members->at(member);
        for(int parent = 0; parent < curr->parents->size(); parent += 1) {
            s->insert(curr->parents->at(parent));
        }
    }
    return s;
}

NECNode* RewriteToNECTree(query_node_map* qg, QueryNode* start, 
                          unordered_set<NECNode*>* leaf_nec_nodes) {
    NECNode* curr;
    NECNode* root;
    vector<NECNode*>* curr_level_nec = new vector<NECNode*>;
    vector<NECNode*>* next_level_nec = new vector<NECNode*>;
    vector<NECNode*>* tmp;
    vector<QueryNode*> candidate_set;
    unordered_set<QueryNode*> visited;
    unordered_set<QueryNode*>* adj_query_nodes;

    curr = NECNodeInit();
    root = curr;
    curr->members->push_back(start);
    visited.insert(start);
    next_level_nec->push_back(curr);
    do {
        // Swap curr and next literation
        tmp = curr_level_nec;
        curr_level_nec = next_level_nec;
        next_level_nec = tmp;
        tmp->clear();

        for (int nec_idx = 0; nec_idx < curr_level_nec->size(); nec_idx += 1) {
            curr = curr_level_nec->at(nec_idx);

            // Set of adj child QueryNodes
            adj_query_nodes = GetAdjacentChildQueryNodes(curr);
            vector<NECNode*>* children = GenerateNECNodes(adj_query_nodes, &visited);
            curr->children = children;
            for (int i = 0; i < children->size(); i += 1)
                next_level_nec->push_back(children->at(i));
            delete adj_query_nodes;

            // Set of adj parent QueryNodes
            adj_query_nodes = GetAdjacentParentQueryNodes(curr);
            vector<NECNode*>* parents = GenerateNECNodes(adj_query_nodes, &visited);
            curr->parents = parents;
            for (int i = 0; i < parents->size(); i += 1)
                next_level_nec->push_back(parents->at(i));
            delete adj_query_nodes;

            if (curr->children->size() == 0) {
                leaf_nec_nodes->insert(curr);
            }

            cout << "NEC Node ID :" << curr << " Children:";
            for (int i = 0; i < curr->children->size(); i += 1) {
                cout << curr->children->at(i) << " | ";
            }
            cout << " \t\tParents:";
            for (int i = 0; i < curr->parents->size(); i += 1) {
                cout << curr->parents->at(i) << " | ";
            }
            cout << " \t\t Members:";
            for (int i = 0; i < curr->members->size(); i += 1) {
                cout << curr->members->at(i) << " | ";
            }
            cout << endl;
        }
    } while (curr_level_nec->size() > 0);

    return root;
}

bool DegreeFilter(DataGraph& dg, VERTEX data_node_id, NECNode* nec_root) {
    QueryNode* nec_node = nec_root->members->at(0);
    return nec_node->children->size() == dg.efs->GetOutDegree(data_node_id);
}

/* TODO : Sort adjacency list of query nodes by ascending label so we can do
 * this quickly
bool NeighborLabelFilter(DataGraph& dg, UINT data_node_id, NECNode* nec_root) {
    QueryNode* nec_node = nec_root->members->at(0);

}
*/

NEC_PQ* GetNECEvalOrder(DataGraph& dg, NECNode* nec_root, 
                        VERTEX v_index, int parent_flag) {
    vector<NECNode*>* nec_nodes;
    vector<NECNode*>::iterator it;
    pair<NECNode*, UINT>* p;
    NECNode* curr;
    NEC_PQ* pq = new NEC_PQ;

    if (parent_flag)
        nec_nodes = nec_root->parents;
    else 
        nec_nodes = nec_root->children;
    for (it = nec_nodes->begin(); it != nec_nodes->end(); it++) {
        curr = *(it);
        p = new pair<NECNode*, UINT>;
        p->first = curr;
        if (parent_flag)
            p->second = dg.efs->GetInDegree(v_index, curr->members->at(0)->label);
        else
            p->second = dg.efs->GetOutDegree(v_index, curr->members->at(0)->label);
        pq->push(p);
    }

    return pq;
}

void ClearCandidateRegions(CandidateRegions* cr, 
                           vector<NECNode*>* visited_nec_neighbors,
                           VERTEX parent)
{
    NECNode* clear_nec;

    for (int neighbor = 0; neighbor < visited_nec_neighbors->size(); neighbor += 1) 
    {
        clear_nec = visited_nec_neighbors->at(neighbor);
        if (cr->count(clear_nec) > 0 &&
            cr->find(clear_nec)->second->count(parent) > 0) {
            cr->find(clear_nec)->second->find(parent)->second->clear();
        }
    }
}

void InsertIntoCandidateRegion(CandidateRegions* cr, 
                               VERTEX parent,
                               NECNode* nec_node,
                               VERTEX vertex_id) {
    unordered_map<VERTEX, vector<VERTEX>*>* prior_vertex_map;

    cout << "Insert into candidate regions: parent - " << parent << " vertex - " << nec_node << endl;

    if (cr->count(nec_node) == 0) {
        cr->insert(make_pair<NECNode*, unordered_map<VERTEX, vector<VERTEX>*>*>
                        (nec_node, new unordered_map<VERTEX, vector<VERTEX>*>));
        cout << "CR ADD " << nec_node << endl;
    }

    prior_vertex_map = cr->find(nec_node)->second;
    if (prior_vertex_map->count(parent) == 0) {
        prior_vertex_map->insert(make_pair<VERTEX, vector<VERTEX>*>
                                          (parent, new vector<VERTEX>));
        cout << "CR PARENT ADD " << parent << endl;
    }
    prior_vertex_map->find(parent)->second->push_back(vertex_id);
}

bool ExploreSubRegions(DataGraph& dg,
                       unordered_set<VERTEX>* marked_vertex, 
                       CandidateRegions* cr,
                       VERTEX parent,
                       VERTEX adj_vertex,
                       vector<NECNode*>* visited_nec_neighbors,
                       NEC_PQ* pq)
{
    NECNode* adj_nec;
    vector<UINT>* candidate_vertices;
    LABEL candidate_label;
    bool matched = true;

    if (pq->size() > 0) {
        adj_nec = pq->top()->first;
        candidate_label = adj_nec->members->at(0)->label;
        candidate_vertices = dg.efs->GetOutdegreeVertices(adj_vertex, 
                                                        candidate_label);
        // Construct candidate vertex using VFS
        if (ExploreCandidateRegions(dg, adj_nec, candidate_vertices,
                                    marked_vertex,
                                    cr, adj_vertex) == false)
        {
            ClearCandidateRegions(cr, visited_nec_neighbors, parent);
            matched = false;
        }
        visited_nec_neighbors->push_back(adj_nec);
        delete pq->top();
        pq->pop();
        delete candidate_vertices;
    }
    return matched;
}

bool ExploreCandidateRegions(DataGraph& dg, NECNode* nec_root,
                             vector<VERTEX>* data_node_candidates,
                             unordered_set<VERTEX>* marked_vertex, 
                             CandidateRegions* cr,
                             VERTEX parent)
{
    vector<VERTEX>::iterator it;

    NEC_PQ* child_pq;
    NEC_PQ* parent_pq;

    vector<NECNode*> visited_nec_neighbors;
    VERTEX data_node_id;
    bool matched;

    cout << "CR - Data node candidates " ;
    for (int i = 0; i < data_node_candidates->size(); i += 1) {
        cout << data_node_candidates->at(i) << " ";
    }
    cout << endl;
    it = data_node_candidates->begin();
    for(; it != data_node_candidates->end(); ++it) {
        data_node_id = *(it);
        if (marked_vertex->count(data_node_id) > 0 ||
            DegreeFilter(dg, data_node_id, nec_root) == true)
            //NeighborLabelFilter(dg, data_node_id, nec_root))
            continue;

        marked_vertex->insert(data_node_id);
        matched = true;

        // Need to sort children
        child_pq = GetNECEvalOrder(dg, nec_root, data_node_id, CHILD);
        parent_pq = GetNECEvalOrder(dg, nec_root, data_node_id, PARENT);

        // Recursively generate CR for child NEC, by interleaving parent and
        // child NEC nodes.
        while (child_pq->size() > 0 || parent_pq->size() > 0) {
            if(ExploreSubRegions(dg, marked_vertex, cr, parent, data_node_id, 
                                 &visited_nec_neighbors, child_pq) == false) {
                matched = false;
                break;
            }
            if (ExploreSubRegions(dg, marked_vertex, cr, parent, data_node_id, 
                                  &visited_nec_neighbors, parent_pq) == false) {
                matched = false;
                break;
            }
        }

        marked_vertex->erase(data_node_id);
        if (matched == false) {
            cout << "Matched False :" << parent << " , " << nec_root << endl;
            continue;
        }

        // Insert into candidate region 
        InsertIntoCandidateRegion(cr, parent, nec_root, data_node_id);
    }

    cout << "Query:" << nec_root << "_Parent:" << parent << " ";
    cout << cr->count(nec_root) << endl;

    if (cr->count(nec_root) == 0) {
        return false;
    }

    if (cr->find(nec_root)->second->find(parent)->second->size() 
                < nec_root->members->size())
    {
        cr->find(nec_root)->second->find(parent)->second->clear();
        return false;
    }
    return true;
}

vector<NECNode*>* MatchingOrderGetUnvisited(NECNode* leaf, 
                                        unordered_set<NECNode*>* visited) {
    vector<NECNode*>* v = new vector<NECNode*>;
    NECNode* curr;
    for (int i = 0; i < leaf->children->size(); i += 1) {
        curr = leaf->children->at(i);
        if (visited->count(curr) == 0)
            v->push_back(curr);
    }
    for (int i = 0; i < leaf->parents->size(); i += 1) {
        curr = leaf->parents->at(i);
        if (visited->count(curr) == 0)
            v->push_back(curr);
    }
    return v;
}

void MatchingOrderVisitChild(NECNode* child, vector<NECNode*>* path, ULONG nontree_edges,
                             queue<pair<vector<NECNode*>*, ULONG>*>* bfs_queue,
                             unordered_set<NECNode*>* visited) {
    vector<NECNode*>* new_path;
    pair<vector<NECNode*>*, ULONG>* new_pair;

    if (visited->count(child) > 0) {
        // Already marked
        return;
    }

    visited->insert(child);
    new_path = new vector<NECNode*>(*path);

    // TODO : remove
    cout << "*** copied vector size: " << new_path->size() << endl;
    for (int i = 0; i < new_path->size(); i += 1)
        cout << new_path->at(i) << endl;
    cout << "printed copied queue" << endl;

    new_path->push_back(child);
    new_pair = new pair<vector<NECNode*>*, ULONG>;
    new_pair->first = new_path;
    new_pair->second = nontree_edges;
    bfs_queue->push(new_pair);
}

ULONG multiplyRange(ULONG start, ULONG end) {
    if (start == end)
        return start;
    return multiplyRange(start, end/2) * multiplyRange(end/2 + 1, end);
}

ULONG xChoseY(ULONG x, ULONG y) {
    return multiplyRange(y+1, x);
}

// TODO : New scoring that sums all NEC nodes not only leaf? Leaf 
// may have few candidate but intermediate NEC may have many.
ULONG MatchingOrderCalcScore(vector<NECNode*>* path, ULONG nontree_edges, CandidateRegions* cr) {
    unordered_map<VERTEX, vector<VERTEX>*>* cr_nodes;
    unordered_map<VERTEX, vector<VERTEX>*>::iterator it;
    NECNode* leaf = path->back();
    ULONG NEC_member_count = path->back()->members->size();
    ULONG score = 0;

    if (cr->count(leaf) == 0) {
        cout << "Invalid candidate region" << endl;
    }
    cr_nodes = cr->at(leaf);
    it = cr_nodes->begin();
    if ( NEC_member_count > 1) {

        while (it != cr_nodes->end()) {
            score += xChoseY(it->second->size(), NEC_member_count);
            ++ it;
        }
    } else {
        while (it != cr_nodes->end()) {
            score += it->second->size();
            ++it;
        }
        score = score / (nontree_edges + 1);
    }
    return score;
}

vector<NECNode*>* GenerateNECOrder(MatchingOrderPq* path_pq) {
    unordered_set<NECNode*> seen_nec;
    vector<NECNode*>* nec_queue;
    MatchingOrderPair* pair;
    NECNode* curr_nec;
    vector<NECNode*>* evaluation_queue = new vector<NECNode*>;

    while (!path_pq->empty()) {
        pair = path_pq->top();
        path_pq->pop();
        nec_queue = pair->q;
        
        for (int index = nec_queue->size() - 1; index >= 0; index -= 1) {
            curr_nec = nec_queue->at(index);
            if (seen_nec.count(curr_nec) > 0)
                continue;

            seen_nec.insert(curr_nec);
            evaluation_queue->push_back(curr_nec);
        }
    }

    return evaluation_queue;
}

void MatchingOrderPrint(MatchingOrderPq* pq) {
    cout << " Matching Order " << endl;
    cout << "Pq Size " << pq->size() << endl;
    while (!pq->empty()) {
        MatchingOrderPair* pair = pq->top();
        pq->pop();
        vector<NECNode*>* q = pair->q;

        cout << "path size " << q->size() << endl;
        for (int i = 0; i < q->size(); i += 1) {
            cout << pair << " " << q->at(i) << " - ";
        }
        cout << endl;
    }
}

MatchingOrderPq* MatchingOrder(NECNode* nec_root, CandidateRegions* cr) {
    ULONG new_nontree_edges;
    ULONG nontree_edges;
    vector<NECNode*>* path;
    NECNode* leaf_node;
    NECNode* child;
    vector<NECNode*>* NEC_to_visit;
    pair<vector<NECNode*>*, ULONG>* p;
    pair<vector<NECNode*>*, ULONG>* new_pair;
    MatchingOrderPair* pq_elem;

    queue<pair<vector<NECNode*>*, ULONG>* > bfs_queue;
    MatchingOrderPq* pq = new MatchingOrderPq;
    unordered_set<NECNode*> visited;
    vector<NECNode*>* seed_queue = new vector<NECNode*>;

    new_pair = new pair<vector<NECNode*>*, ULONG>;
    new_pair->first = seed_queue;
    new_pair->second = 0;
    seed_queue->push_back(nec_root);
    bfs_queue.push(new_pair);
    visited.insert(nec_root);

    while (bfs_queue.empty() == false) {
        p = bfs_queue.front();
        bfs_queue.pop();
        path = p->first;
        leaf_node = path->back();
        NEC_to_visit = MatchingOrderGetUnvisited(leaf_node, &visited);
        new_nontree_edges = leaf_node->parents->size() +
                            leaf_node->children->size() -
                            NEC_to_visit->size();
        nontree_edges = p->second + new_nontree_edges;

        for (int i = 1; i < NEC_to_visit->size(); i += 1) {
            child = NEC_to_visit->at(i);
            MatchingOrderVisitChild(child, path, nontree_edges, &bfs_queue, &visited);
        }

        if (NEC_to_visit->size() > 0) {
            // Reuse allocated queue for 0-th child.
            child = NEC_to_visit->at(0);
            MatchingOrderVisitChild(child, path, nontree_edges, &bfs_queue, &visited);
        }

        if (NEC_to_visit->size() == 0) {
            // End of path
            pq_elem = new MatchingOrderPair;
            pq_elem->q = path;
            pq_elem->score = MatchingOrderCalcScore(path, nontree_edges, cr);
            pq->push(pq_elem);

            cout << "****** copied queue size: " << path->size() << endl;
            for (int i = 0; i < path->size(); i += 1) {
                cout << path->at(i) << endl;
            }
            cout << "printed copied queue" << endl;
        }
    }

    return pq;
}

CandidateRegions* AllocCandidateRegions(DataGraph& dg, NECNode* nec_root, LABEL root_label) {
    CandidateRegions* cr = new CandidateRegions;
    unordered_set<VERTEX>* visited_vertices = new unordered_set<VERTEX>;
    VERTEX root_node_id;
    vector<VERTEX> candidate_data_vertices;
    VERTEX* data_node_iter;

    data_node_iter = dg.vfs->GetVertexIterator(root_label);
    for(int data_node_index = 0; 
        data_node_index < dg.vfs->NumVertices(root_label); 
        data_node_index += 1)
    {
        root_node_id = *(data_node_iter + data_node_index);
        candidate_data_vertices.clear();
        candidate_data_vertices.push_back(root_node_id);

        if(ExploreCandidateRegions(dg, nec_root, &candidate_data_vertices, 
                                   visited_vertices, cr, -1) == false)
        {
            continue;
        }
        CandidateRegions::iterator it = cr->begin();
        while (it != cr->end()) {
            cout << "NEC NODE :" << it->first << " - \n";
            unordered_map<VERTEX, vector<VERTEX>*>::iterator it2 = it->second->begin();
            while (it2 != it->second->end()) {
                cout << "\t\tDataNode vertex prior: " << it2->first << " - ";
                vector<VERTEX>* v = it2->second;
                for (int i = 0; i < v->size(); i += 1) {
                    cout << v->at(i) << " ";
                }
                cout << endl;
                it2++;
            }
            it ++;
        }
        // Iterate and print me
    }
    return cr;
}


void TurboIso(DataGraph& dg, query_node_map* qg) {
    QueryNode* start_vertex;
    CandidateRegions* cr;
    MatchingOrderPq* matching_order_pq;
    LABEL root_label;
    unordered_set<NECNode*>* leaf_nec_nodes = new unordered_set<NECNode*>;
    NECNode* nec_root;
    vector<NECNode*>* matching_queue;

    start_vertex = GetStartingQueryVertex(dg, qg);
    root_label = start_vertex->label;
    nec_root = RewriteToNECTree(qg, start_vertex, leaf_nec_nodes);
    cr = AllocCandidateRegions(dg, nec_root, root_label);
    matching_order_pq = MatchingOrder(nec_root, cr);
    // MatchingOrderPrint(matching_order);
    matching_queue = GenerateNECOrder(matching_order_pq);
    
    cout << "NEC QUEUE :";
    for (int i = 0; i < matching_queue->size(); i += 1) {
        cout << matching_queue->at(i) << " ";
    }
    cout << endl;
}

int main() {
    DataGraph dg;
    query_node_map* qg;

    VertexFs vfs("test.txt");
    EdgeFs efs("test_efs.txt", &vfs);
    dg.vfs = &vfs;
    dg.efs = &efs;

    qg = ReadQueryGraphFromFile("query_graph.txt");
    cout << "print query graph" << endl;
    PrintQueryGraph(qg);

    TurboIso(dg, qg);
}
#endif
