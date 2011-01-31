/**********************************************************************
 * $Id: SegmentString.h 1872 2006-10-20 11:18:39Z strk $
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

#ifndef GEOS_NODING_SEGMENTSTRING_H
#define GEOS_NODING_SEGMENTSTRING_H

#include <geos/noding/SegmentNodeList.h>
#include <geos/geom/CoordinateSequence.h> // for testInvariant

#include <geos/inline.h>

#include <vector>

// Forward declarations
namespace geos {
	namespace algorithm {
		class LineIntersector;
	}
}

namespace geos {
namespace noding { // geos.noding

/** \brief
 * Represents a list of contiguous line segments,
 * and supports noding the segments.
 *
 * The line segments are represented by a CoordinateSequence.
 *
 * TODO:
 * This should be changed to use a vector of Coordinate,
 * to optimize the noding of contiguous segments by
 * reducing the number of allocated objects.
 *
 * SegmentStrings can carry a context object, which is useful
 * for preserving topological or parentage information.
 * All noded substrings are initialized with the same context object.
 *
 * Final class.
 *
 * Last port: noding/SegmentString.java rev. 1.5 (JTS-1.7)
 */
class SegmentString {
public:
	typedef std::vector<const SegmentString*> ConstVect;
	typedef std::vector<SegmentString *> NonConstVect;

	friend std::ostream& operator<< (std::ostream& os,
			const SegmentString& ss);

private:
	SegmentNodeList nodeList;
	geom::CoordinateSequence *pts;
	mutable unsigned int npts; // this is a cache
	const void* context;
	bool isIsolatedVar;

public:

	void testInvariant() const;

	/// Construct a SegmentString.
	//
	/// @param newPts CoordinateSequence representing the string,
	/// externally owned
	///
	/// @param newContext the context associated to this SegmentString
	///
	SegmentString(geom::CoordinateSequence *newPts, const void* newContext);

	~SegmentString();

	//const void* getContext() const { return getData(); }

	const void* getData() const;

	const SegmentNodeList& getNodeList() const;

	SegmentNodeList& getNodeList();

	unsigned int size() const;

	const geom::Coordinate& getCoordinate(unsigned int i) const;

	/// \brief
	/// Return a pointer to the CoordinateSequence associated
	/// with this SegmentString.
	//
	/// Note that the CoordinateSequence is not owned by
	/// this SegmentString!
	///
	geom::CoordinateSequence* getCoordinates() const;

	/// \brief
	/// Notify this object that the CoordinateSequence associated
	/// with it might have been updated.
	//
	/// This must be called so that the SegmentString object makes
	/// all the necessary checks and updates to verify consistency
	///
	void notifyCoordinatesChange() const;

	// Return a read-only pointer to this SegmentString CoordinateSequence
	//const CoordinateSequence* getCoordinatesRO() const { return pts; }

	void setIsolated(bool isIsolated);

	bool isIsolated() const;
	
	bool isClosed() const;

	/** \brief
	 * Gets the octant of the segment starting at vertex
	 * <code>index</code>.
	 *
	 * @param index the index of the vertex starting the segment. 
	 *              Must not be the last index in the vertex list
	 * @return the octant of the segment at the vertex
	 */
	int getSegmentOctant(unsigned int index) const;

	/** \brief
	 * Add {SegmentNode}s for one or both
	 * intersections found for a segment of an edge to the edge
	 * intersection list.
	 */
	void addIntersections(algorithm::LineIntersector *li,
			unsigned int segmentIndex, int geomIndex);

	/** \brief
	 * Add an SegmentNode for intersection intIndex.
	 *
	 * An intersection that falls exactly on a vertex
	 * of the SegmentString is normalized
	 * to use the higher of the two possible segmentIndexes
	 */
	void addIntersection(algorithm::LineIntersector *li,
			unsigned int segmentIndex,
			int geomIndex, int intIndex);

	/** \brief
	 * Add an SegmentNode for intersection intIndex.
	 *
	 * An intersection that falls exactly on a vertex of the
	 * edge is normalized
	 * to use the higher of the two possible segmentIndexes
	 */
	void addIntersection(const geom::Coordinate& intPt,
			unsigned int segmentIndex);

	static void getNodedSubstrings(
			const SegmentString::NonConstVect& segStrings,
			SegmentString::NonConstVect* resultEdgeList);

	static SegmentString::NonConstVect* getNodedSubstrings(
			const SegmentString::NonConstVect& segStrings);
};

inline void
SegmentString::testInvariant() const
{
	assert(pts);
	assert(pts->size() > 1);
	assert(pts->size() == npts);
}

std::ostream& operator<< (std::ostream& os, const SegmentString& ss);

} // namespace geos.noding
} // namespace geos

#ifdef GEOS_INLINE
# include "geos/noding/SegmentString.inl"
#endif

#endif

/**********************************************************************
 * $Log$
 * Revision 1.10  2006/05/05 14:25:05  strk
 * moved getSegmentOctant out of .inl into .cpp, renamed private eiList to nodeList as in JTS, added more assertion checking and fixed doxygen comments
 *
 * Revision 1.9  2006/05/05 10:19:06  strk
 * droppped SegmentString::getContext(), new name is getData() to reflect change in JTS
 *
 * Revision 1.8  2006/05/04 08:29:07  strk
 * * source/noding/ScaledNoder.cpp: removed use of SegmentString::setCoordinates().
 * * source/headers/geos/noding/SegmentStrign.{h,inl}: removed new setCoordinates() interface.
 *
 * Revision 1.7  2006/05/04 07:43:44  strk
 * output operator for SegmentString class
 *
 * Revision 1.6  2006/05/03 18:04:49  strk
 * added SegmentString::setCoordinates() interface
 *
 * Revision 1.5  2006/05/03 16:19:39  strk
 * fit in 80 columns
 *
 * Revision 1.4  2006/05/03 15:26:02  strk
 * testInvariant made public and always inlined
 *
 * Revision 1.3  2006/03/24 09:52:41  strk
 * USE_INLINE => GEOS_INLINE
 *
 * Revision 1.2  2006/03/13 21:14:24  strk
 * Added missing forward declarations
 *
 * Revision 1.1  2006/03/09 16:46:49  strk
 * geos::geom namespace definition, first pass at headers split
 *
 **********************************************************************/

