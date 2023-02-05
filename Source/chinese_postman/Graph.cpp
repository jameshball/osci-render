#include "Graph.h"

Graph::Graph(int n, const list< pair<int, int> > & edges):
	n(n),
	m(edges.size()),
	adjMat(n * n),
	adjList(n),
	edges(),
	edgeIndex(n, vector<int>(n, -1))
{
	for(list< pair<int, int> >::const_iterator it = edges.begin(); it != edges.end(); it++)
	{
		int u = (*it).first;
		int v = (*it).second;

		AddEdge(u, v);
	}
}

pair<int, int> Graph::GetEdge(int e) const
{
	return edges[e];
}

int Graph::GetEdgeIndex(int u, int v) const
{
	return edgeIndex[u][v];
}

void Graph::AddEdge(int u, int v)
{
	if(adjMat[u * n + v]) return;

	adjMat[u * n + v] = adjMat[v * n + u] = true;
	adjList[u].push_back(v);
	adjList[v].push_back(u);

	edges.push_back(pair<int, int>(u, v));
	edgeIndex[u][v] = edgeIndex[v][u] = m++;
}

const vector<int>& Graph::AdjList(int v) const
{
	if(v > n)
		throw "Error: vertex does not exist";

	return adjList[v];
}

const vector<bool> & Graph::AdjMat() const
{
	return adjMat;
}

