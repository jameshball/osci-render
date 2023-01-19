#pragma once

#include <list>
#include <vector>
using namespace std;

class Graph
{
public:
	//n is the number of vertices
	//edges is a list of pairs representing the edges (default = empty list)
	Graph(int n, const list< pair<int, int> > & edges = list< pair<int, int> >());

	//Default constructor creates an empty graph
	Graph(): n(0), m(0) {};

	//Returns the number of vertices
	int GetNumVertices() const { return n; };
	//Returns the number of edges
	int GetNumEdges() const { return m; };

	//Given the edge's index, returns its endpoints as a pair
	pair<int, int> GetEdge(int e) const;
	//Given the endpoints, returns the index
	int GetEdgeIndex(int u, int v) const;

	//Adds a new vertex to the graph
	void AddVertex();
	//Adds a new edge to the graph
	void AddEdge(int u, int v);

	//Returns the adjacency list of a vertex
	const list<int> & AdjList(int v) const;

	//Returns the graph's adjacency matrix
	const vector< vector<bool> > & AdjMat() const;
private:
	//Number of vertices
	int n;
	//Number of edges
	int m;

	//Adjacency matrix
	vector< vector<bool> > adjMat;

	//Adjacency lists
	vector< list<int> > adjList;

	//Array of edges
	vector< pair<int, int> > edges;

	//Indices of the edges
	vector< vector<int> > edgeIndex;
};
