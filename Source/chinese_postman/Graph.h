#pragma once

#include <list>
#include <vector>
#include <unordered_map>
using namespace std;

class Graph
{
public:
	//n is the number of vertices
	//edges is a list of pairs representing the edges (default = empty list)
	Graph(int n, list< pair<int, int> > & edges);

	//Default constructor creates an empty graph
	Graph(): n(0), m(0) {};

	//Returns the number of vertices
	int GetNumVertices() { return n; };
	//Returns the number of edges
	int GetNumEdges() { return m; };

	//Given the edge's index, returns its endpoints as a pair
	pair<int, int> GetEdge(int e);
	//Given the endpoints, returns the index
	int GetEdgeIndex(int u, int v);

	//Adds a new edge to the graph
	void AddEdge(int u, int v);

	//Returns the adjacency list of a vertex
	const vector<int>& AdjList(int v);

	//Returns the graph's adjacency matrix
	const vector<bool>& AdjMat();
private:
	//Number of vertices
	int n;
	//Number of edges
	int m;

	//Adjacency matrix
	vector<bool> adjMat;

	//Adjacency lists
	vector<vector<int> > adjList;

	//Array of edges
	vector<pair<int, int> > edges;

	//Indices of the edges
	unordered_map<int, int> edgeIndex;
};
