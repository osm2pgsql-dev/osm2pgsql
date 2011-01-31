/**********************************************************************
 * $Id: SegmentNode.h 1820 2006-09-06 16:54:23Z mloskot $
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

#ifndef GEOS_NODING_SEGMENTNODE_H
#define GEOS_NODING_SEGMENTNODE_H

#include <vector>
#include <iostream>

#include <geos/inline.h>

#include <geos/geom/Coordinate.h>

// Forward declarations
namespace geos {
	namespace noding {
		class SegmentString;
	}
}

namespace geos {
namespace noding { // geos.noding

/// Represents an intersection point between two SegmentString.
//
/// Final class.
///
/// Last port: noding/SegmentNode.java rev. 1.5 (JTS-1.7)
///
class SegmentNode {
private:
	const SegmentString& segString;

	int segmentOctant;

	bool isInteriorVar;

public:
	friend std::ostream& operator<< (std::ostream& os, const SegmentNode& n);

	/// the point of intersection (own copy)
	geom::Coordinate coord;  

	/// the index of the containing line segment in the parent edge
	unsigned int segmentIndex;  

	/// Construct a node on the given SegmentString
	//
	/// @param ss the parent SegmentString 
	///
	/// @param coord the coordinate of the intersection, will be copied
	///
	/// @param nSegmentIndex the index of the segment on parent SegmentString
	///        where the Node is located.
	///
	/// @param nSegmentOctant
	///
	SegmentNode(const SegmentString& ss, const geom::Coordinate& nCoord,
			unsigned int nSegmentIndex, int nSegmentOctant);

	~SegmentNode() {}

	/// \brief
	/// Return true if this Node is *internal* (not on the boundary)
	/// of the corresponding segment. Currently only the *first*
	/// segment endpoint is checked, actually.
	///
	bool isInterior() const { return isInteriorVar; }

	bool isEndPoint(unsigned int maxSegmentIndex) const;

	/**
	 * @return -1 this EdgeIntersection is located before
	 *            the argument location
	 * @return 0 this EdgeIntersection is at the argument location
	 * @return 1 this EdgeIntersection is located after the
	 *           argument location
	 */
	int compareTo(const SegmentNode& other);

	//string print() const;
};

std::ostream& operator<< (std::ostream& os, const SegmentNode& n);

struct SegmentNodeLT {
	bool operator()(SegmentNode *s1, SegmentNode *s2) const {
		return s1->compareTo(*s2)<0;
	}
};


} // namespace geos.noding
} // namespace geos

//#ifdef GEOS_INLINE
//# include "geos/noding/SegmentNode.inl"
//#endif

#endif // GEOS_NODING_SEGMENTNODE_H

/**********************************************************************
 * $Log$
 * Revision 1.2  2006/03/24 09:52:41  strk
 * USE_INLINE => GEOS_INLINE
 *
 * Revision 1.1  2006/03/09 16:46:49  strk
 * geos::geom namespace definition, first pass at headers split
 *
 **********************************************************************/

