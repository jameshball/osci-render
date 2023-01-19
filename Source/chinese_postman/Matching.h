#pragma once

#include "Graph.h"
#include "BinaryHeap.h"
#include <list>
#include <vector>
using namespace std;

#define EVEN 2
#define ODD 1
#define UNLABELED 0

class Matching
{
public:
	//Parametric constructor receives a graph instance
	Matching(const Graph & G);

	//Solves the minimum cost perfect matching problem
	//Receives the a vector whose position i has the cost of the edge with index i
	//If the graph doest not have a perfect matching, a const char * exception will be raised
	//Returns a pair
	//the first element of the pair is a list of the indices of the edges in the matching
	//the second is the cost of the matching
	pair< list<int>, double > SolveMinimumCostPerfectMatching(const vector<double> & cost);

	//Solves the maximum cardinality matching problem
	//Returns a list with the indices of the edges in the matching
	list<int> SolveMaximumMatching();

private:
	//Grows an alternating forest
	void Grow();
	//Expands a blossom u
	//If expandBlocked is true, the blossom will be expanded even if it is blocked
	void Expand(int u, bool expandBlocked);
	//Augments the matching using the path from u to v in the alternating forest
	void Augment(int u, int v);
	//Resets the alternating forest
	void Reset();
	//Creates a blossom where the tip is the first common vertex in the paths from u and v in the hungarian forest
	int Blossom(int u, int v);
	void UpdateDualCosts();
	//Resets all data structures 
	void Clear();
	void DestroyBlossom(int t);
	//Uses an heuristic algorithm to find the maximum matching of the graph
	void Heuristic();
	//Modifies the costs of the graph so the all edges have positive costs
	void PositiveCosts();
	list<int> RetrieveMatching();

	int GetFreeBlossomIndex();
	void AddFreeBlossomIndex(int i);
	void ClearBlossomIndices();

	//An edge might be blocked due to the dual costs
	bool IsEdgeBlocked(int u, int v);
	bool IsEdgeBlocked(int e);
	//Returns true if u and v are adjacent in G and not blocked
	bool IsAdjacent(int u, int v);

	const Graph & G;

	list<int> free;//List of free blossom indices

	vector<int> outer;//outer[v] gives the index of the outermost blossom that contains v, outer[v] = v if v is not contained in any blossom
	vector< list<int> > deep;//deep[v] is a list of all the original vertices contained inside v, deep[v] = v if v is an original vertex
	vector< list<int> > shallow;//shallow[v] is a list of the vertices immediately contained inside v, shallow[v] is empty is the default
	vector<int> tip;//tip[v] is the tip of blossom v
	vector<bool> active;//true if a blossom is being used

	vector<int> type;//Even, odd, neither (2, 1, 0)
	vector<int> forest;//forest[v] gives the father of v in the alternating forest
	vector<int> root;//root[v] gives the root of v in the alternating forest 

	vector<bool> blocked;//A blossom can be blocked due to dual costs, this means that it behaves as if it were an original vertex and cannot be expanded
	vector<double> dual;//dual multipliers associated to the blossoms, if dual[v] > 0, the blossom is blocked and full
	vector<double> slack;//slack associated to each edge, if slack[e] > 0, the edge cannot be used
	vector<int> mate;//mate[v] gives the mate of v

	int m, n;

	bool perfect;

	list<int> forestList;
	vector<int> visited;
};

