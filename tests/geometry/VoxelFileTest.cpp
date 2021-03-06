//======================================================================================================================
//
//  This file is part of waLBerla. waLBerla is free software: you can 
//  redistribute it and/or modify it under the terms of the GNU General Public
//  License as published by the Free Software Foundation, either version 3 of 
//  the License, or (at your option) any later version.
//  
//  waLBerla is distributed in the hope that it will be useful, but WITHOUT 
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
//  for more details.
//  
//  You should have received a copy of the GNU General Public License along
//  with waLBerla (see COPYING.txt). If not, see <http://www.gnu.org/licenses/>.
//
//! \file VoxelFileTest.cpp
//! \ingroup geometry
//! \author Christian Godenschwager <christian.godenschwager@fau.de>
//
//======================================================================================================================



#ifdef _MSC_VER
#  pragma warning(push)
// disable warning boost/concept/detail/msvc.hpp(22): warning C4100: 'x' : unreferenced formal parameter
#  pragma warning( disable : 4100 )
// disable warning bboost/concept/detail/msvc.hpp(76): warning C4067 : unexpected tokens following preprocessor directive - expected a newline
#  pragma warning( disable : 4067 )

#include <boost/concept/detail/msvc.hpp>

#  pragma warning(pop)
#endif //_MSC_VER

#ifdef _MSC_VER
#  pragma warning(push)
// disable warning boost/multi_array/base.hpp(475): warning C4189: 'bound_adjustment' : local variable is initialized but not referenced
#  pragma warning( disable : 4189 )
#endif //_MSC_VER

#include <boost/multi_array/base.hpp>

#ifdef _MSC_VER
#  pragma warning(pop)
#endif //_MSC_VER

#include "geometry/structured/VoxelFileReader.h"
#include "core/Abort.h"
#include "core/DataTypes.h"
#include "core/debug/TestSubsystem.h"
#include "core/logging/Logging.h"
#include "core/mpi/MPIManager.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>

#ifdef _MSC_VER
#  pragma warning(push)
// disable warning for multi_array: "declaration of 'extents' hides global declaration"
#  pragma warning( disable : 4459 )
#endif //_MSC_VER

#include <boost/multi_array.hpp>

#ifdef _MSC_VER
#  pragma warning(pop)
#endif //_MSC_VER


/// randomize the memory underlying the vector up the maximal size (== capacity)
template<typename T>
void randomizeVector( std::vector<T> & v )
{
   //boost::random::mt11213b rng;
   boost::mt11213b rng;
   //boost::random::uniform_int_distribution<T> dist( std::numeric_limits<T>::min(), std::numeric_limits<T>::max() );
   boost::uniform_int<T> dist( std::numeric_limits<T>::min(), std::numeric_limits<T>::max() );

   size_t oldSize = v.size();
   v.resize( v.capacity() );
   for(typename std::vector<T>::iterator it = v.begin(); it != v.end(); ++it)
      *it = dist(rng);
   v.resize(oldSize);
}

template<typename T>
void makeRandomMultiArray( boost::multi_array<T, 3> & ma)
{
   //boost::random::mt11213b rng;
   boost::mt11213b rng;
   //boost::random::uniform_int_distribution<T> dist( std::numeric_limits<T>::min(), std::numeric_limits<T>::max() );
   boost::uniform_int<T> dist( std::numeric_limits<T>::min(), std::numeric_limits<T>::max() );

   for(T* it = ma.data(); it != ma.data() + ma.num_elements(); ++it)
      *it = dist(rng);
}

template<typename T>
void runTests(const std::string & filename, size_t xSize, size_t ySize, size_t zSize);

template<typename T>
void makeRandomMultiArray( boost::multi_array<T, 3> & ma);

void modifyHeader(std::string inputFilename, std::string outputFilename,
                  size_t xSize, size_t ySize, size_t zSize);

template<typename T>
void makeRandomMultiArray( boost::multi_array<T, 3> & ma);

int main(int argc, char** argv)
{
   walberla::debug::enterTestMode();
   walberla::MPIManager::instance()->initializeMPI( &argc, &argv );

   WALBERLA_LOG_INFO("Starting test!");

   bool longrun = std::find(argv, argv + argc, std::string("--longrun")) != argv + argc;

   std::vector<size_t> sizes;
   if(longrun)
   {
      sizes.push_back(1);
      sizes.push_back(2);
      sizes.push_back(9);
      sizes.push_back(10);
   }
   sizes.push_back(111);

   for(std::vector<size_t>::const_iterator xSize = sizes.begin(); xSize != sizes.end(); ++xSize)
      for(std::vector<size_t>::const_iterator ySize = sizes.begin(); ySize != sizes.end(); ++ySize)
         for(std::vector<size_t>::const_iterator zSize = sizes.begin(); zSize != sizes.end(); ++zSize)
         {
            std::stringstream ss;
            ss << "geometry_testfile_" << *xSize << "_" << *ySize << "_" << *zSize << ".dat";

            std::string filename = ss.str();

            runTests<unsigned char>(filename, *xSize, *ySize, *zSize);

            if(longrun)
            {
               runTests<char>(filename, *xSize, *ySize, *zSize);

               runTests<short>(filename, *xSize, *ySize, *zSize);
               runTests<unsigned short>(filename, *xSize, *ySize, *zSize);

               runTests<int>(filename, *xSize, *ySize, *zSize);
               runTests<unsigned int>(filename, *xSize, *ySize, *zSize);

               runTests<long>(filename, *xSize, *ySize, *zSize);
               runTests<unsigned long>(filename, *xSize, *ySize, *zSize);
            }
         }
}

template<typename T>
void runTests(const std::string & filename, size_t xSize, size_t ySize, size_t zSize)
{
   using namespace walberla;
   using geometry::VoxelFileReader;

   namespace fs = boost::filesystem;

   WALBERLA_LOG_INFO( "Running Test with size " << xSize << "x" << ySize << "x" << zSize << " T = " << typeid(T).name() );

   fs::path path(filename);

   if( fs::exists( path ) )
      fs::remove( path );

   CellInterval aabb(0, 0, 0, cell_idx_c(xSize - 1), cell_idx_c(ySize - 1), cell_idx_c(zSize - 1));
   uint_t numCells = aabb.numCells();

   VoxelFileReader<T> geometryFile(filename, xSize, ySize, zSize);

   WALBERLA_CHECK( geometryFile.isOpen() );
   WALBERLA_CHECK_EQUAL( geometryFile.filename(), filename );
   WALBERLA_CHECK_EQUAL( geometryFile.xSize(), xSize );
   WALBERLA_CHECK_EQUAL( geometryFile.ySize(), ySize );
   WALBERLA_CHECK_EQUAL( geometryFile.zSize(), zSize );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );


   std::vector<T> data;
   randomizeVector(data);
   geometryFile.read(aabb, data);
   WALBERLA_CHECK_EQUAL( numCells, data.size() );

   WALBERLA_CHECK_EQUAL( xSize, geometryFile.xSize() );
   WALBERLA_CHECK_EQUAL( ySize, geometryFile.ySize() );
   WALBERLA_CHECK_EQUAL( zSize, geometryFile.zSize() );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );

   ptrdiff_t numDefaultConstructedElements = std::count( data.begin(), data.end(), T() );
   WALBERLA_CHECK_EQUAL( numCells, numDefaultConstructedElements );

   geometryFile.close();
   WALBERLA_CHECK( !geometryFile.isOpen() );

   data.clear();

   WALBERLA_CHECK( fs::exists( path ) );
   WALBERLA_CHECK( fs::is_regular_file( path ) );

   geometryFile.open(filename);
   WALBERLA_CHECK( geometryFile.isOpen() );
   WALBERLA_CHECK_EQUAL( geometryFile.filename(), filename );
   WALBERLA_CHECK_EQUAL( geometryFile.xSize(), xSize );
   WALBERLA_CHECK_EQUAL( geometryFile.ySize(), ySize );
   WALBERLA_CHECK_EQUAL( geometryFile.zSize(), zSize );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );

   randomizeVector(data);
   geometryFile.read(aabb, data);
   WALBERLA_CHECK_EQUAL( numCells, data.size() );
   WALBERLA_CHECK_EQUAL( xSize, geometryFile.xSize() );
   WALBERLA_CHECK_EQUAL( ySize, geometryFile.ySize() );
   WALBERLA_CHECK_EQUAL( zSize, geometryFile.zSize() );

   numDefaultConstructedElements = std::count( data.begin(), data.end(), T() );
   WALBERLA_CHECK_EQUAL( numCells, numDefaultConstructedElements );

   geometryFile.close();
   WALBERLA_CHECK( !geometryFile.isOpen() );

   geometryFile.create(filename, xSize, ySize, zSize, 7);
   WALBERLA_CHECK( geometryFile.isOpen() );
   WALBERLA_CHECK_EQUAL( geometryFile.filename(), filename );
   WALBERLA_CHECK_EQUAL( geometryFile.xSize(), xSize );
   WALBERLA_CHECK_EQUAL( geometryFile.ySize(), ySize );
   WALBERLA_CHECK_EQUAL( geometryFile.zSize(), zSize );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );

   data.clear();
   randomizeVector(data);
   geometryFile.read(aabb, data);

   WALBERLA_CHECK_EQUAL( numCells, data.size() );
   WALBERLA_CHECK_EQUAL( xSize, geometryFile.xSize() );
   WALBERLA_CHECK_EQUAL( ySize, geometryFile.ySize() );
   WALBERLA_CHECK_EQUAL( zSize, geometryFile.zSize() );

   numDefaultConstructedElements = std::count( data.begin(), data.end(), numeric_cast<T>(7) );
   WALBERLA_CHECK_EQUAL( numCells, numDefaultConstructedElements );

   data.clear();

   typedef boost::multi_array_types::index bindex;

   boost::multi_array<T, 3> reference(boost::extents[walberla::numeric_cast<bindex>(zSize)][walberla::numeric_cast<bindex>(ySize)][walberla::numeric_cast<bindex>(xSize)]);
   makeRandomMultiArray(reference);

   data.resize(reference.num_elements());
   std::copy( reference.data(), reference.data() + reference.num_elements(), data.begin() );

   geometryFile.open(filename);
   WALBERLA_CHECK( geometryFile.isOpen() );
   WALBERLA_CHECK_EQUAL( geometryFile.filename(), filename );
   WALBERLA_CHECK_EQUAL( geometryFile.xSize(), xSize );
   WALBERLA_CHECK_EQUAL( geometryFile.ySize(), ySize );
   WALBERLA_CHECK_EQUAL( geometryFile.zSize(), zSize );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );

   geometryFile.write(aabb, data);
   data.clear();
   randomizeVector(data);
   geometryFile.read(aabb, data);

   WALBERLA_CHECK_EQUAL(reference.num_elements(), data.size());
   WALBERLA_CHECK( std::equal(reference.data(), reference.data() + reference.num_elements(), data.begin()) );

   randomizeVector(data);

   geometryFile.create(filename, xSize, ySize, zSize, reference.data() );
   WALBERLA_CHECK( geometryFile.isOpen() );
   WALBERLA_CHECK_EQUAL( geometryFile.filename(), filename );
   WALBERLA_CHECK_EQUAL( geometryFile.xSize(), xSize );
   WALBERLA_CHECK_EQUAL( geometryFile.ySize(), ySize );
   WALBERLA_CHECK_EQUAL( geometryFile.zSize(), zSize );
   WALBERLA_CHECK_EQUAL( geometryFile.numCells(), xSize * ySize* zSize );

   geometryFile.read(aabb, data);

   WALBERLA_CHECK_EQUAL(reference.num_elements(), data.size());
   WALBERLA_CHECK( std::equal(reference.data(), reference.data() + reference.num_elements(), data.begin()) );

   std::vector<size_t> blockSizesX;
   blockSizesX.push_back(std::max(xSize / 2, size_t(1)));
   if( xSize > size_t(1) )
      blockSizesX.push_back(std::max(xSize / 2 - 1, size_t(1)));

   std::vector<size_t> blockSizesY;
   blockSizesY.push_back(std::max(ySize / 2, size_t(1)));
   if( ySize > size_t( 1 ) )
      blockSizesY.push_back(std::max(ySize / 2 - 1, size_t(1)));

   std::vector<size_t> blockSizesZ;
   blockSizesZ.push_back(std::max(zSize / 2, size_t(1)));
   if( zSize > size_t( 1 ) )
      blockSizesZ.push_back(std::max(zSize / 2 - 1, size_t(1)));

   for( auto xit = blockSizesX.begin(); xit != blockSizesX.end(); ++xit ) { size_t blockSizeX = *xit;
      for( auto yit = blockSizesY.begin(); yit != blockSizesY.end(); ++yit ) { size_t blockSizeY = *yit;
         for( auto zit = blockSizesZ.begin(); zit != blockSizesZ.end(); ++zit ) { size_t blockSizeZ = *zit;
            for(size_t zz = 0; zz <= (zSize - 1) / blockSizeZ; ++zz) {
               for(size_t yy = 0; yy <= (ySize - 1) / blockSizeY; ++yy) {
                  for(size_t xx = 0; xx <= (xSize - 1) / blockSizeX; ++xx)
                  {
                     CellInterval blockAABB;
                     blockAABB.xMin() = cell_idx_c(xx * blockSizeX);
                     blockAABB.yMin() = cell_idx_c(yy * blockSizeY);
                     blockAABB.zMin() = cell_idx_c(zz * blockSizeZ);
                     blockAABB.xMax() = cell_idx_c(std::min(blockAABB.xMin() + cell_idx_c(blockSizeX), cell_idx_c(geometryFile.xSize())) - 1);
                     blockAABB.yMax() = cell_idx_c(std::min(blockAABB.yMin() + cell_idx_c(blockSizeY), cell_idx_c(geometryFile.ySize())) - 1);
                     blockAABB.zMax() = cell_idx_c(std::min(blockAABB.zMin() + cell_idx_c(blockSizeZ), cell_idx_c(geometryFile.zSize())) - 1);
                     WALBERLA_CHECK_LESS(blockAABB.xMin(), xSize);
                     WALBERLA_CHECK_LESS(blockAABB.yMin(), ySize);
                     WALBERLA_CHECK_LESS(blockAABB.zMin(), zSize);
                     WALBERLA_CHECK_LESS(blockAABB.xMax(), xSize);
                     WALBERLA_CHECK_LESS(blockAABB.yMax(), ySize);
                     WALBERLA_CHECK_LESS(blockAABB.zMax(), zSize);

                     geometryFile.read(blockAABB, data);
                     WALBERLA_CHECK_EQUAL( data.size(), blockAABB.numCells() );

                     typename boost::multi_array<T, 3>::template array_view<3>::type myview =
                        reference[ boost::indices[boost::multi_array_types::index_range(blockAABB.zMin(), blockAABB.zMax() + 1)]
                                                 [boost::multi_array_types::index_range(blockAABB.yMin(), blockAABB.yMax() + 1)]
                                                 [boost::multi_array_types::index_range(blockAABB.xMin(), blockAABB.xMax() + 1)] ];

                     size_t vectorIdx = 0;
                     for(size_t z = 0; z < blockAABB.zSize(); ++z)
                        for(size_t y = 0; y < blockAABB.ySize(); ++y)
                           for(size_t x = 0; x < blockAABB.xSize(); ++x)
                           {
                              WALBERLA_CHECK_EQUAL(data[vectorIdx], myview[walberla::numeric_cast<bindex>(z)][walberla::numeric_cast<bindex>(y)][walberla::numeric_cast<bindex>(x)]);
                              ++vectorIdx;
                           }
                  }
               }
            }
         }
      }
   }

   geometryFile.close();
   WALBERLA_CHECK( !geometryFile.isOpen() );

   if( fs::exists( path ) )
      fs::remove( path );

   geometryFile.create(filename, xSize, ySize, zSize);

   for( auto xit = blockSizesX.begin(); xit != blockSizesX.end(); ++xit ) { size_t blockSizeX = *xit;
      for( auto yit = blockSizesY.begin(); yit != blockSizesY.end(); ++yit ) { size_t blockSizeY = *yit;
         for( auto zit = blockSizesZ.begin(); zit != blockSizesZ.end(); ++zit ) { size_t blockSizeZ = *zit;
            for(size_t zz = 0; zz <= (zSize - 1) / blockSizeZ; ++zz) {
               for(size_t yy = 0; yy <= (ySize - 1) / blockSizeY; ++yy) {
                  for(size_t xx = 0; xx <= (xSize - 1) / blockSizeX; ++xx)
                  {
                     CellInterval blockAABB;
                     blockAABB.xMin() = cell_idx_c(xx * blockSizeX);
                     blockAABB.yMin() = cell_idx_c(yy * blockSizeY);
                     blockAABB.zMin() = cell_idx_c(zz * blockSizeZ);
                     blockAABB.xMax() = cell_idx_c(std::min(blockAABB.xMin() + cell_idx_c(blockSizeX), cell_idx_c(geometryFile.xSize())) - 1);
                     blockAABB.yMax() = cell_idx_c(std::min(blockAABB.yMin() + cell_idx_c(blockSizeY), cell_idx_c(geometryFile.ySize())) - 1);
                     blockAABB.zMax() = cell_idx_c(std::min(blockAABB.zMin() + cell_idx_c(blockSizeZ), cell_idx_c(geometryFile.zSize())) - 1);
                     WALBERLA_CHECK_LESS(blockAABB.xMin(), xSize);
                     WALBERLA_CHECK_LESS(blockAABB.yMin(), ySize);
                     WALBERLA_CHECK_LESS(blockAABB.zMin(), zSize);
                     WALBERLA_CHECK_LESS(blockAABB.xMax(), xSize);
                     WALBERLA_CHECK_LESS(blockAABB.yMax(), ySize);
                     WALBERLA_CHECK_LESS(blockAABB.zMax(), zSize);

                     typename boost::multi_array<T, 3>::template array_view<3>::type myview =
                        reference[ boost::indices[boost::multi_array_types::index_range(blockAABB.zMin(), blockAABB.zMax() + 1)]
                                                 [boost::multi_array_types::index_range(blockAABB.yMin(), blockAABB.yMax() + 1)]
                                                 [boost::multi_array_types::index_range(blockAABB.xMin(), blockAABB.xMax() + 1)] ];

                     data.resize(blockAABB.numCells());
                     size_t vectorIdx = 0;
                     for(size_t z = 0; z < blockAABB.zSize(); ++z)
                        for(size_t y = 0; y < blockAABB.ySize(); ++y)
                           for(size_t x = 0; x < blockAABB.xSize(); ++x)
                           {
                              data[vectorIdx] = myview[walberla::numeric_cast<bindex>(z)][walberla::numeric_cast<bindex>(y)][walberla::numeric_cast<bindex>(x)];
                              ++vectorIdx;
                           }

                     geometryFile.write(blockAABB, data);
                  }
               }
            }
            geometryFile.read(aabb, data);
            WALBERLA_CHECK_EQUAL(reference.num_elements(), data.size());
            WALBERLA_CHECK( std::equal(reference.data(), reference.data() + reference.num_elements(), data.begin()) );
         }
      }
   }

   modifyHeader(filename, filename + "0", xSize + 1, ySize, zSize);
   bool runtimeErrorThrown = false;
   try
   {
      WALBERLA_LOG_INFO("The following Error is expected!");
      Abort::instance()->resetAbortFunction( &Abort::exceptionAbort );
      geometryFile.open(filename + "0");
      Abort::instance()->resetAbortFunction();
      WALBERLA_CHECK(false);
   }
   catch( const std::runtime_error & /*e*/ )
   {
      Abort::instance()->resetAbortFunction();
      runtimeErrorThrown = true;
   }
   WALBERLA_CHECK( runtimeErrorThrown );
   geometryFile.close();
   if( fs::exists( fs::path(filename + "0") ) )
      fs::remove( fs::path(filename + "0") );

   if(xSize > 0)
   {
      modifyHeader(filename, filename + "1", xSize - 1, ySize, zSize);
      runtimeErrorThrown = false;
      try
      {
         WALBERLA_LOG_INFO("The following Error is expected!");
         Abort::instance()->resetAbortFunction( &Abort::exceptionAbort );
         geometryFile.open(filename + "1");
         Abort::instance()->resetAbortFunction();
         WALBERLA_CHECK(false);
      }
      catch( const std::runtime_error & /*e*/ )
      {
         Abort::instance()->resetAbortFunction();
         runtimeErrorThrown = true;
      }
      WALBERLA_CHECK( runtimeErrorThrown );
      geometryFile.close();
      if( fs::exists( fs::path(filename + "1") ) )
         fs::remove( fs::path(filename + "1") );
   }

   if( fs::exists( path ) )
      fs::remove( path );

}

void modifyHeader(std::string inputFilename, std::string outputFilename,
                  size_t xSize, size_t ySize, size_t zSize)
{
   std::ifstream is(inputFilename.c_str(), std::fstream::in | std::fstream::binary);
   WALBERLA_CHECK( is.is_open() );
   std::string oldHeader;
   std::getline(is, oldHeader);

   std::ofstream os(outputFilename.c_str(), std::fstream::out | std::fstream::binary);
   WALBERLA_CHECK( os.is_open() );
   os << xSize << " " << ySize << " " << zSize << "\n";

   while(!is.eof())
   {
      char buffer[1024];
      is.read(buffer, 1024);
      std::streamsize bytesRead = is.gcount();
      os.write( buffer, bytesRead );
      WALBERLA_CHECK( is.eof() || !is.fail() );
      WALBERLA_CHECK( !os.fail() );
   }

   is.close();
   os.close();
}


