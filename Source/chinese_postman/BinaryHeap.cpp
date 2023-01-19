#include "BinaryHeap.h"

void BinaryHeap::Clear()
{
	key.clear();
	pos.clear();
	satellite.clear();
}

void BinaryHeap::Insert(double k, int s)
{
	//Ajust the structures to fit new data
	if(s >= (int)pos.size())
	{
		pos.resize(s+1, -1);
		key.resize(s+1);
		//Recall that position 0 of satellite is unused
		satellite.resize(s+2);
	}
	//If satellite is already in the heap
	else if(pos[s] != -1)
	{
		throw "Error: satellite already in heap";
	}

	int i;
	for(i = ++size; i/2 > 0 && GREATER(key[satellite[i/2]], k); i /= 2)
	{
		satellite[i] = satellite[i/2];
		pos[satellite[i]] = i;
	}
	satellite[i] = s;
	pos[s] = i;
	key[s] = k;
}

int BinaryHeap::Size()
{
	return size;
}

int BinaryHeap::DeleteMin()
{
	if(size == 0)
		throw "Error: empty heap";

	int min = satellite[1];
	int slast = satellite[size--];


	int child;
	int i;
	for(i = 1, child = 2; child  <= size; i = child, child *= 2)
	{
		if(child < size && GREATER(key[satellite[child]], key[satellite[child+1]]))
			child++;

		if(GREATER(key[slast], key[satellite[child]]))
		{
			satellite[i] = satellite[child];
			pos[satellite[child]] = i;
		}
		else
			break;
	}
	satellite[i] = slast;
	pos[slast] = i;

	pos[min] = -1;

	return min;
}

void BinaryHeap::ChangeKey(double k, int s)
{
	Remove(s);
	Insert(k, s);
}

void BinaryHeap::Remove(int s)
{
	int i;
	for(i = pos[s]; i/2 > 0; i /= 2)
	{
		satellite[i] = satellite[i/2];
		pos[satellite[i]] = i;
	}
	satellite[1] = s;
	pos[s] = 1;

	DeleteMin();
}

