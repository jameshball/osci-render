#include "./Graph.h"
#include "ChinesePostman.h"
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
using namespace std;

pair< Graph, vector<double> > ReadWeightedGraph(string filename)
{
	//Please see Graph.h for a description of the interface

	ifstream file;
	file.open(filename.c_str());

	string s;
	getline(file, s);
	stringstream ss(s);
	int n;
	ss >> n;
	getline(file, s);
	ss.str(s);
	ss.clear();
	int m;
	ss >> m;

	Graph G(n);
	vector<double> cost(m);
	for(int i = 0; i < m; i++)
	{
		getline(file, s);
		ss.str(s);
		ss.clear();
		int u, v;
		double c;
		ss >> u >> v >> c;

		G.AddEdge(u, v);
		cost[G.GetEdgeIndex(u, v)] = c;
	}

	file.close();
	return make_pair(G, cost);
}

int main(int argc, char * argv[])
{
	string filename = "";

	int i = 1;
	while(i < argc)
	{
		string a(argv[i]);
		if(a == "-f")
			filename = argv[++i];
		i++;
	}

	if(filename == "")
	{
		cout << endl << "usage: ./example -f <filename>" << endl << endl;
		cout << "-f followed by file name to specify the input file." << endl << endl;
		
		cout << "File format:" << endl;
		cout << "The first two lines give n (number of vertices) and m (number of edges)," << endl;
		cout << "Each of the next m lines has a tuple (u, v [, c]) representing an edge," << endl;
	   	cout << "where u and v are the endpoints (0-based indexing) of the edge and c is its cost" << endl;
		return 1;
	}

	try
	{
	    Graph G;
	    vector<double> cost;
	
	    //Read the graph
	    pair< Graph, vector<double> > p = ReadWeightedGraph(filename);
	    G = p.first;
	    cost = p.second;

	    //Solve the problem
     	pair< list<int> , double > sol = ChinesePostman(G, cost);

		cout << "Solution cost: " << sol.second << endl;

		list<int> s = sol.first;

        //Print edges in the solution
		cout << "Solution:" << endl;
		for(list<int>::iterator it = s.begin(); it != s.end(); it++)
			cout << *it << " ";
		cout << endl;
	}
	catch(const char * msg)
	{
		cout << msg << endl;
		return 1;
	}

	return 0;
}


