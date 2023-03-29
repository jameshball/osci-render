#include "Graph.h"

Graph::Graph(int n, list< pair<int, int> > & edges):
	n(n),
	m(edges.size()),
	adjMat(n * n),
	adjList(n),
	edges(),
	edgeIndex()
{
	for(list< pair<int, int> >::const_iterator it = edges.begin(); it != edges.end(); it++)
	{
		int u = (*it).first;
		int v = (*it).second;

		AddEdge(u, v);
	}
}

pair<int, int> Graph::GetEdge(int e)
{
	return edges[e];
}

int Graph::GetEdgeIndex(int u, int v) {
	return edgeIndex[u * n + v];
}

void Graph::AddEdge(int u, int v)
{
	if(adjMat[u * n + v]) return;

	adjMat[u * n + v] = adjMat[v * n + u] = true;
	adjList[u].push_back(v);
	adjList[v].push_back(u);

	edges.push_back(pair<int, int>(u, v));
	edgeIndex[u * n + v] = edgeIndex[v * n + u] = m++;
}

const vector<int>& Graph::AdjList(int v)
{
	if(v > n)
		throw "Error: vertex does not exist";

	return adjList[v];
}

const vector<bool> & Graph::AdjMat()
{
	return adjMat;
}

