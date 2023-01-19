#pragma once

#include "./Graph.h"
#include "./BinaryHeap.h"
#include "./Globals.h"
#include <limits>
using namespace std;

//Dijkstra's algorithm using binary heap
//Returns a pair (vector<int>, vector<double>)
//vector<double> gives the cost of the optimal path to each vertex
//vector<int> gives the parent of each vertex in the tree of optimal paths
pair< vector<int>, vector<double> > Dijkstra(const Graph & G, int origin, const vector<double> & cost)
{
	BinaryHeap B;

	int n = G.GetNumVertices();

	//Father of each vertex in the optimal path tree
	vector<int> father(n, -1);

	//Used to indicate whether a vertex is permanently labeled
	vector<bool> permanent(n, false);

	vector<double> pathCost(n, numeric_limits<double>::infinity());
	
	//Put s in the heap
	B.Insert(0, origin);
	pathCost[origin] = 0;

	for(int i = 0; i < n; i++)
	{
		//Select the vertex that can be reached with smallest cost
		int u = B.DeleteMin();

		permanent[u] = true;

		//Update the heap with vertices adjacent to u
		for(list<int>::const_iterator it = G.AdjList(u).begin(); it != G.AdjList(u).end(); it++)
		{
			int v = *it;
			
			if(permanent[v])
				continue;

			double c = pathCost[u] + (cost.empty() ? 1.0 : cost[G.GetEdgeIndex(u,v)]);

			//v has not been discovered yet
			if(father[v] == -1)
			{
				father[v] = u;	
				pathCost[v] = c;
				B.Insert(c, v);
			}
			//we found a cheaper connection to v
			else if( LESS(c, pathCost[v]) )
			{
				father[v] = u;
				pathCost[v] = c;
				B.ChangeKey(c, v);
			}
		}
	}

	if(B.Size() > 0)
		throw "Error: graph is not connected";

	return make_pair(father, pathCost);
}



