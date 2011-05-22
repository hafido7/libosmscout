#ifndef OSMSCOUT_AREAINDEX_H
#define OSMSCOUT_AREAINDEX_H

/*
  This source is part of the libosmscout library
  Copyright (C) 2010  Tim Teulings

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include <map>
#include <vector>

#include <osmscout/StyleConfig.h>

#include <osmscout/Util.h>

#include <osmscout/util/Cache.h>
#include <osmscout/util/FileScanner.h>

namespace osmscout {

  /**
    AreaIndex allows you to find areas, ways, area relations and way releations in
    a given area.

    For area structure result can be limited by the maximum level (which in turn
    defines the mimimum size of the resulting areas since and area in a given
    level must fit into the cell size (but can cross cell borders)) and the
    maximum number of areas found.

    Way in turn can be limited by type and result count.

    Internally the index is implemented as quadtree. As a result each index entry
    has 4 children (besides entries in the lowest level).
    */
  class AreaIndex
  {
  private:
    /**
      Datastructure for every index entry of our index.
      */
    struct IndexEntry
    {
      FileOffset              children[4]; //! File index of each of the four children, or 0 if there is no child
      std::vector<FileOffset> areas;
      std::vector<FileOffset> relAreas;
    };

    typedef Cache<FileOffset,IndexEntry> IndexCache;

    struct IndexCacheValueSizer : public IndexCache::ValueSizer
    {
      unsigned long GetSize(const IndexEntry& value) const
      {
        unsigned long memory=0;

        memory+=sizeof(value);

        // Areas
        memory+=value.areas.size()*sizeof(FileOffset);

        // RelAreas
        memory+=value.relAreas.size()*sizeof(FileOffset);

        return memory;
      }
    };

  private:
    std::string                     filepart;       //! name of the data file
    std::string                     datafilename;   //! Fullpath and name of the data file
    mutable FileScanner             scanner;        //! Scanner instance for reading this file

    std::vector<double>             cellWidth;      //! Precalculated cellWidth for each level of the quadtree
    std::vector<double>             cellHeight;     //! Precalculated cellHeight for each level of the quadtree
    uint32_t                        maxLevel;       //! Maximum level in index
    FileOffset                      topLevelOffset; //! Offset o fthe top level index entry

    mutable IndexCache              indexCache;     //! Cached map of all index entries by file offset

  private:
    bool GetIndexEntry(uint32_t level,
                       FileOffset offset,
                       IndexCache::CacheRef& cacheRef) const;

  public:
    AreaIndex(size_t cacheSize);

    bool Load(const std::string& path);

    bool GetOffsets(const StyleConfig& styleConfig,
                    double minlon,
                    double minlat,
                    double maxlon,
                    double maxlat,
                    size_t maxAreaLevel,
                    size_t maxAreaCount,
                    std::vector<FileOffset>& wayAreaOffsets,
                    std::vector<FileOffset>& relationAreaOffsets) const;

    void DumpStatistics();
  };
}

#endif
