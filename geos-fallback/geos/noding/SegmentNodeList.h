/**********************************************************************
 * $Id: SegmentNodeList.h 1820 2006-09-06 16:54:23Z mloskot $
 *
 * GEOS - Geometry Engine Open Source
 * http://geos.refractions.net
 *
 * Copyright (C) 2006      Refractions Research Inc.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Public Licence as published
 * by the Free Software Foundation. 
 * See the COPYING file for more information.
 *
 **********************************************************************/

#ifndef GEOS_NODING_SEGMENTNODELIST_H
#define GEOS_NODING_SEGMENTNODELIST_H

#include <geos/inline.h>

#include <cassert>
#include <iostream>
#include <vector>
#include <set>

#include <geos/noding/SegmentNode.h>

// Forward declarations
namespace geos {
	namespace geom {
		class CoordinateSequence;
	}
	namespace noding {
		class SegmentString;
	}
}

namespace geos {
namespace noding { // geos::noding

/** \brief
 * A list of the SegmentNode present along a
 * noded SegmentString.
 *
 * Last port: noding/SegmentNodeList.java rev. 1.7 (JTS-1.7)
 */
class SegmentNodeList {
private:
	std::set<SegmentNode*,SegmentNodeLT> nodeMap;

	// the parent edge
	const SegmentString& edge; 

	// UNUSED
	//std::vector<SegmentNode*> *sortedNodes;

	// This vector is here to keep track of created splitEdges
	std::vector<SegmentString*> splitEdges;

	// This vector is here to keep track of created Coordinates
	std::vector<geom::CoordinateSequence*> splitCoordLists;

	/**
	 * Checks the correctness of the set of split edges corresponding
	 * to this edge
	 *
	 * @param splitEdges the split edges for this edge (in order)
	 */
	void checkSplitEdgesCorrectness(std::vector<SegmentString*>& splitEdges);

	/**
	 * Create a new "split edge" with the section of points between
	 * (and including) the two intersections.
	 * The label for the new edge is the same as the label for the
	 * parent edge.
	 */
	SegmentString* createSplitEdge(SegmentNode *ei0, SegmentNode *ei1);

	/**
	 * Adds nodes for any collapsed edge pairs.
	 * Collapsed edge pairs can be caused by inserted nodes, or they
	 * can be pre-existing in the edge vertex list.
	 * In order to provide the correct fully noded semantics,
	 * the vertex at the base of a collapsed pair must also be added
	 * as a node.
	 */
	void addCollapsedNodes();

	/**
	 * Adds nodes for any collapsed edge pairs
	 * which are pre-existing in the vertex list.
	 */
	void findCollapsesFromExistingVertices(
			std::vector<size_t>& collapsedVertexIndexes);

	/**
	 * Adds nodes for any collapsed edge pairs caused by inserted nodes
	 * Collapsed edge pairs occur when the same coordinate is inserted
	 * as a node both before and after an existing edge vertex.
	 * To provide the correct fully noded semantics,
	 * the vertex must be added as a node as well.
	 */
	void findCollapsesFromInsertedNodes(
		std::vector<size_t>& collapsedVertexIndexes);

	bool findCollapseIndex(SegmentNode& ei0, SegmentNode& ei1,
		size_t& collapsedVertexIndex);
public:

	friend std::ostream& operator<< (std::ostream& os, const SegmentNodeList& l);

	typedef std::set<SegmentNode*,SegmentNodeLT> container;
	typedef container::iterator iterator;
	typedef container::const_iterator const_iterator;

	SegmentNodeList(const SegmentString* newEdge): edge(*newEdge) {}

	SegmentNodeList(const SegmentString& newEdge): edge(newEdge) {}

	const SegmentString& getEdge() const { return edge; }

	// TODO: Is this a final class ?
	// Should remove the virtual in that case
	virtual ~SegmentNodeList();

	/**
	 * Adds an intersection into the list, if it isn't already there.
	 * The input segmentIndex is expected to be normalized.
	 *
	 * @return the SegmentIntersection found or added. It will be
	 *	   destroyed at SegmentNodeList destruction time.
	 *
	 * @param intPt the intersection Coordinate, will be copied
	 * @param segmentIndex 
	 */
	SegmentNode* add(const geom::Coordinate& intPt, size_t segmentIndex);

	SegmentNode* add(const geom::Coordinate *intPt, size_t segmentIndex) {
		return add(*intPt, segmentIndex);
	}

	/*
	 * returns the set of SegmentNodes
	 */
	//replaces iterator()
	// TODO: obsolete this function
	std::set<SegmentNode*,SegmentNodeLT>* getNodes() { return &nodeMap; }

	/// Return the number of nodes in this list
	size_t size() const { return nodeMap.size(); }

	container::iterator begin() { return nodeMap.begin(); }
	container::const_iterator begin() const { return nodeMap.begin(); }
	container::iterator end() { return nodeMap.end(); }
	container::const_iterator end() const { return nodeMap.end(); }

	/**
	 * Adds entries for the first and last points of the edge to the list
	 */
	void addEndpoints();

	/**
	 * Creates new edges for all the edges that the intersections in this
	 * list split the parent edge into.
	 * Adds the edges to the input list (this is so a single list
	 * can be used to accumulate all split edges for a Geometry).
	 */
	void addSplitEdges(std::vector<SegmentString*>& edgeList);

	void addSplitEdges(std::vector<SegmentString*>* edgeList) {
		assert(edgeList);
		addSplitEdges(*edgeList);
	}

	//string print();
};

std::ostream& operator<< (std::ostream& os, const SegmentNodeList& l);

} // namespace geos::noding
} // namespace geos

//#ifdef GEOS_INLINE
//# include "geos/noding/SegmentNodeList.inl"
//#endif

#endif

/**********************************************************************
 * $Log$
 * Revision 1.4  2006/06/12 11:29:23  strk
 * unsigned int => size_t
 *
 * Revision 1.3  2006/05/04 07:41:56  strk
 * const-correct size() method for SegmentNodeList
 *
 * Revision 1.2  2006/03/24 09:52:41  strk
 * USE_INLINE => GEOS_INLINE
 *
 * Revision 1.1  2006/03/09 16:46:49  strk
 * geos::geom namespace definition, first pass at headers split
 *
 **********************************************************************/

