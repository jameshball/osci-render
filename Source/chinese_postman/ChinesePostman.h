#include "Dijkstra.h"
#include "./Matching.h"
#include "./Graph.h"

bool Connected(const Graph & G)
{
    vector<bool> visited(G.GetNumVertices(), false);
    list<int> L;
    
    int n = 0;
    L.push_back(0);
    while(not L.empty())
    {
        int u = L.back();
        L.pop_back();
        if(visited[u]) continue;
        
        visited[u] = true;
        n++;
        
        for(int v : G.AdjList(u)) {
		    L.push_back(v);
		}
    }
   
    return G.GetNumVertices() == n;
}

/*
Solves the chinese postman problem
returns a pair containing a list and a double
the list is the sequence of vertices in the solution
the double is the solution cost
*/
pair< list<int>, double > ChinesePostman(const Graph& G, const vector<double>& cost = vector<double>())
{
	//Check if the graph if connected
	if(not Connected(G))
		throw "Error: Graph is not connected";

	//Build adjacency lists using edges in the graph
	vector<vector<int>> A(G.GetNumVertices(), vector<int>());
	for(int u = 0; u < G.GetNumVertices(); u++)
	    A[u] = G.AdjList(u);

	//Find vertices with odd degree
	vector<int> odd;
	for(int u = 0; u < G.GetNumVertices(); u++)
		if(A[u].size() % 2)
			odd.push_back(u);

    //If there are odd degree vertices
	if(not odd.empty())
	{
		//Create a graph with the odd degree vertices
		Graph O(odd.size());
		for(int u = 0; u < (int)odd.size(); u++)
			for(int v = u+1; v < (int)odd.size(); v++)
				O.AddEdge(u, v);

        vector<double> costO(O.GetNumEdges());
        
        //Find the shortest paths between all odd degree vertices
		vector< vector<int> > shortestPath(O.GetNumVertices());
		for(int u = 0; u < (int)odd.size(); u++)
		{
			pair< vector<int>, vector<double> > sp = Dijkstra(G, odd[u], cost);
			
			shortestPath[u] = sp.first ;
			
			//The cost of an edge uv in O will be the cost of the corresponding shortest path in G
			for(int v = 0; v < (int)odd.size(); v++)
			    if(v != u)
    			    costO[ O.GetEdgeIndex(u, v) ] = sp.second[odd[v]];
		}

	    //Find the minimum cost perfect matching of the graph of the odd degree vertices
	    Matching M(O);
	    pair< list<int>, double > p = M.SolveMinimumCostPerfectMatching(costO);
	    list<int> matching = p.first;
	    
	    //If an edge uv is in the matching, the edges in the shortest path from u to v should be duplicated in G
	    for(list<int>::iterator it = matching.begin(); it != matching.end(); it++)
	    {
		    pair<int, int> p = O.GetEdge(*it);
		    int u = p.first, v = odd[p.second];
		    
		    //Go through the path adding the edges
		    int w = shortestPath[u][v];
		    while(w != -1)
		    {
		        A[w].push_back(v);
		        A[v].push_back(w);
		        v = w;
		        w = shortestPath[u][v];
		    }
	    }
	}

	//Find an Eulerian cycle in the graph implied by A
	list<int> cycle;
	//This is to keep track of how many times we can traverse an edge
	vector<int> traversed(G.GetNumEdges(), 0);
	for(int u = 0; u < G.GetNumVertices(); u++)
	{
		for(int v : A[u]) {
			//we do this so that the edge is not counted twice
			if(v < u) continue;

			traversed[G.GetEdgeIndex(u, v)]++;
		}
	}

	cycle.push_back(0);
	list<int>::iterator itp = cycle.begin();
	double obj = 0;
	while(itp != cycle.end())
	{
		//Let u be the current vertex in the cycle, starting at the first
		int u = *itp;
		list<int>::iterator jtp = itp;
		jtp++;

		//if there are non-traversed edges incident to u, find a subcycle starting at u
		//replace u in the cycle by the subcycle
		while(not A[u].empty())
		{
			while(not A[u].empty() and traversed[ G.GetEdgeIndex(u, A[u].back()) ] == 0)
				A[u].pop_back();

			if(not A[u].empty())
			{
				int v = A[u].back();
				A[u].pop_back();
				cycle.insert(jtp, v);
				traversed[G.GetEdgeIndex(u, v)]--;

		        obj += cost.empty() ? 1.0 : cost[ G.GetEdgeIndex(u, v) ];
				u = v;
			}
		}

		//go to the next vertex in the cycle and do the same
		itp++;
	}

	return pair< list<int>, double >(cycle, obj);
}
