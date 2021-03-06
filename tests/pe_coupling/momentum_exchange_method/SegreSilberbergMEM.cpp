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
//! \file SegreSilberbergMEMPe.cpp
//! \ingroup pe_coupling
//! \author Christoph Rettinger <christoph.rettinger@fau.de>
//
//======================================================================================================================

#include "blockforest/Initialization.h"
#include "blockforest/communication/UniformBufferedScheme.h"

#include "boundary/all.h"

#include "core/DataTypes.h"
#include "core/Environment.h"
#include "core/SharedFunctor.h"
#include "core/debug/Debug.h"
#include "core/debug/TestSubsystem.h"
#include "core/math/all.h"
#include "core/timing/RemainingTimeLogger.h"

#include "domain_decomposition/SharedSweep.h"

#include "field/AddToStorage.h"
#include "field/StabilityChecker.h"
#include "field/communication/PackInfo.h"

#include "lbm/boundary/all.h"
#include "lbm/communication/PdfFieldPackInfo.h"
#include "lbm/field/AddToStorage.h"
#include "lbm/field/PdfField.h"
#include "lbm/lattice_model/D3Q19.h"
#include "lbm/lattice_model/ForceModel.h"
#include "lbm/sweeps/CellwiseSweep.h"
#include "lbm/sweeps/SweepWrappers.h"

#include "pe/basic.h"
#include "pe/vtk/BodyVtkOutput.h"
#include "pe/vtk/SphereVtkOutput.h"
#include "pe/cr/ICR.h"
#include "pe/Types.h"

#include "pe_coupling/mapping/all.h"
#include "pe_coupling/momentum_exchange_method/all.h"
#include "pe_coupling/utility/all.h"

#include "stencil/D3Q27.h"

#include "timeloop/SweepTimeloop.h"

#include "vtk/all.h"
#include "field/vtk/all.h"
#include "lbm/vtk/all.h"

namespace segre_silberberg_mem_pe
{

///////////
// USING //
///////////

using namespace walberla;
using walberla::uint_t;
using lbm::force_model::SimpleConstant;

//////////////
// TYPEDEFS //
//////////////

// PDF field, flag field & body field
typedef lbm::D3Q19< lbm::collision_model::TRT, false, SimpleConstant >  LatticeModel_T;
typedef LatticeModel_T::Stencil          Stencil_T;
typedef lbm::PdfField< LatticeModel_T > PdfField_T;

typedef walberla::uint8_t                 flag_t;
typedef FlagField< flag_t >               FlagField_T;
typedef GhostLayerField< pe::BodyID, 1 >  BodyField_T;

const uint_t FieldGhostLayers = 1;

// boundary handling
typedef lbm::NoSlip< LatticeModel_T, flag_t > NoSlip_T;

typedef pe_coupling::SimpleBB       < LatticeModel_T, FlagField_T >  MO_BB_T;
typedef pe_coupling::CurvedLinear   < LatticeModel_T, FlagField_T > MO_CLI_T;
typedef pe_coupling::CurvedQuadratic< LatticeModel_T, FlagField_T >  MO_MR_T;

typedef boost::tuples::tuple< NoSlip_T, MO_BB_T, MO_CLI_T, MO_MR_T >     BoundaryConditions_T;
typedef BoundaryHandling< FlagField_T, Stencil_T, BoundaryConditions_T > BoundaryHandling_T;

typedef boost::tuple< pe::Sphere, pe::Plane > BodyTypeTuple;

///////////
// FLAGS //
///////////

const FlagUID Fluid_Flag   ( "fluid" );
const FlagUID NoSlip_Flag  ( "no slip" );

const FlagUID MO_BB_Flag   ( "moving obstacle BB" );
const FlagUID MO_CLI_Flag  ( "moving obstacle CLI" );
const FlagUID MO_MR_Flag   ( "moving obstacle MR" );
const FlagUID FormerMO_Flag( "former moving obstacle" );


////////////////
// Parameters //
////////////////

struct Setup
{
   // domain size in x,y,z direction
   uint_t xlength;
   uint_t ylength;
   uint_t zlength;

   // number of block (= processes) in x,y,z, direction
   Vector3<uint_t> nBlocks;

   // cells inside each block in x,y,z direction (uniform distribution)
   Vector3<uint_t> cellsPerBlock;

   real_t particleDiam; // diameter of particle
   real_t nu;           // kin. viscosity
   real_t rho_p;        // particle density
   real_t tau;          // relaxation time
   real_t forcing;      // forcing to drive flow
   real_t Re_p;         // particle Reynoldsnumber
   real_t Re;           // bulk Reynoldsnumber

   uint_t checkFrequency;
   uint_t timesteps;    // maximum number of timesteps to simulate

};

//*******************************************************************************************************************

/////////////////////////////////////
// BOUNDARY HANDLING CUSTOMIZATION //
/////////////////////////////////////

//*******************************************************************************************************************
/*!\brief Responsible for creating the boundary handling and setting up the geometry at the outer domain boundaries
 */
//*******************************************************************************************************************
class MyBoundaryHandling
{
public:

   MyBoundaryHandling( const BlockDataID & flagFieldID, const BlockDataID & pdfFieldID, const BlockDataID & pdfFieldPreColID, const BlockDataID & bodyFieldID ) :
      flagFieldID_( flagFieldID ), pdfFieldID_( pdfFieldID ), pdfFieldPreColID_( pdfFieldPreColID ), bodyFieldID_ ( bodyFieldID ) {}

   BoundaryHandling_T * operator()( IBlock* const block, const StructuredBlockStorage* const storage ) const;

private:

   const BlockDataID flagFieldID_;
   const BlockDataID pdfFieldID_;
   const BlockDataID pdfFieldPreColID_;
   const BlockDataID bodyFieldID_;

}; // class MyBoundaryHandling


BoundaryHandling_T * MyBoundaryHandling::operator()( IBlock * const block, const StructuredBlockStorage * const storage ) const
{
   WALBERLA_ASSERT_NOT_NULLPTR( block );
   WALBERLA_ASSERT_NOT_NULLPTR( storage );

   FlagField_T * flagField       = block->getData< FlagField_T >( flagFieldID_ );
   PdfField_T *  pdfField        = block->getData< PdfField_T > ( pdfFieldID_ );
   PdfField_T *  pdfFieldPreCol  = block->getData< PdfField_T > ( pdfFieldPreColID_ );
   BodyField_T * bodyField       = block->getData< BodyField_T >( bodyFieldID_ );

   const auto fluid = flagField->flagExists( Fluid_Flag ) ? flagField->getFlag( Fluid_Flag ) : flagField->registerFlag( Fluid_Flag );

   BoundaryHandling_T * handling = new BoundaryHandling_T( "moving obstacle boundary handling", flagField, fluid,
         boost::tuples::make_tuple(    NoSlip_T( "NoSlip", NoSlip_Flag, pdfField ),
                                       MO_BB_T (  "MO_BB",  MO_BB_Flag, pdfField, flagField, bodyField, fluid, *storage, *block ),
                                       MO_CLI_T( "MO_CLI", MO_CLI_Flag, pdfField, flagField, bodyField, fluid, *storage, *block ),
                                       MO_MR_T (  "MO_MR",  MO_MR_Flag, pdfField, flagField, bodyField, fluid, *storage, *block, pdfFieldPreCol ) ) );

   const auto noslip = flagField->getFlag( NoSlip_Flag );

   CellInterval domainBB = storage->getDomainCellBB();

   domainBB.xMin() -= cell_idx_c( FieldGhostLayers ); // -1
   domainBB.xMax() += cell_idx_c( FieldGhostLayers ); // cellsX

   domainBB.yMin() -= cell_idx_c( FieldGhostLayers ); // 0
   domainBB.yMax() += cell_idx_c( FieldGhostLayers ); // cellsY+1

   domainBB.zMin() -= cell_idx_c( FieldGhostLayers ); // 0
   domainBB.zMax() += cell_idx_c( FieldGhostLayers ); // cellsZ+1

   // BOTTOM
   CellInterval bottom( domainBB.xMin(), domainBB.yMin(), domainBB.zMin(), domainBB.xMax(), domainBB.yMax(), domainBB.zMin() );
   storage->transformGlobalToBlockLocalCellInterval( bottom, *block );
   handling->forceBoundary( noslip, bottom );

   // TOP
   CellInterval top( domainBB.xMin(), domainBB.yMin(), domainBB.zMax(), domainBB.xMax(), domainBB.yMax(), domainBB.zMax() );
   storage->transformGlobalToBlockLocalCellInterval( top, *block );
   handling->forceBoundary( noslip, top );

   handling->fillWithDomain( FieldGhostLayers );

   return handling;
}
//*******************************************************************************************************************


// initialises the PDF field with a parabolic profile
void initPDF(const shared_ptr< StructuredBlockStorage > & blocks, const BlockDataID & pdfFieldID, Setup & setup)
{
   // iterate all blocks with an iterator 'block'
   for( auto block = blocks->begin(); block != blocks->end(); ++block )
   {
      // get the field data out of the block
      auto pdf = block->getData< PdfField_T > ( pdfFieldID );

      // obtain a CellInterval object that holds information about the number of cells in x,y,z direction of the field excluding ghost layers
      CellInterval xyz = pdf->xyzSize();

      // iterate all (inner) cells in the field
      for( auto cell = xyz.begin(); cell != xyz.end(); ++cell ){

         // obtain the physical coordinate of the center of the current cell
         const Vector3< real_t > p = blocks->getBlockLocalCellCenter( *block, *cell );

         const real_t rho = real_c(1);
         const real_t height = real_c(setup.zlength) * real_c(0.5);

         // parabolic Poiseuille profile for a given external force
         Vector3< real_t > velocity ( setup.forcing / ( real_c(2) * setup.nu) * (height * height - ( p[2] - height ) * ( p[2] - height ) ), real_c(0), real_c(0) );
         pdf->setToEquilibrium( *cell, velocity, rho );
      }
   }
}

//*******************************************************************************************************************
/*!\brief Evaluating the position and velocity of the body
 *
 */
//*******************************************************************************************************************
class SteadyStateCheck
{
   public:
      SteadyStateCheck( SweepTimeloop* timeloop, Setup * setup,
                        const shared_ptr< StructuredBlockStorage > & blocks, const BlockDataID & bodyStorageID,
                        bool fileIO, bool MO_CLI, bool MO_MR, bool eanReconstructor, bool extReconstructor ) :
         timeloop_( timeloop ), setup_(setup),
         blocks_( blocks ), bodyStorageID_( bodyStorageID ),
         fileIO_(fileIO), MO_CLI_(MO_CLI), MO_MR_(MO_MR),
         eanReconstructor_( eanReconstructor ), extReconstructor_( extReconstructor ),
         posOld_(0.0), posNew_(0.0)
      {
         if ( fileIO_ )
         {
            std::ofstream file;
            filename_ = "SegreSilberbergTest_";
            if ( MO_CLI_ )
            {
               filename_ += "CLI";
            }else if ( MO_MR_ )
            {
               filename_ += "MR";
            }else{
               filename_ += "BB";
            }
            if ( eanReconstructor_ )
            {
               filename_ += "_EANRecon";
            }else if ( extReconstructor_ )
            {
               filename_ += "_ExtRecon";
            }else{
               filename_ += "_EquRecon";
            }
            filename_ += ".txt";
            WALBERLA_ROOT_SECTION()
            {
               file.open( filename_.c_str() );
               file << "#\t posX\t posY\t posZ\t velX\t velY\t velZ\n";
               file.close();
            }
         }
      }

      void operator()()
      {
         const uint_t timestep (timeloop_->getCurrentTimeStep()+1);

         if( timestep % setup_->checkFrequency != 0) return;

         pe::Vec3 pos      = pe::Vec3(0.0);
         pe::Vec3 transVel = pe::Vec3(0.0);

         for( auto blockIt = blocks_->begin(); blockIt != blocks_->end(); ++blockIt )
         {
            for( auto bodyIt = pe::LocalBodyIterator::begin( *blockIt, bodyStorageID_); bodyIt != pe::LocalBodyIterator::end(); ++bodyIt )
            {
               pos = bodyIt->getPosition();
               transVel = bodyIt->getLinearVel();
            }
         }

         WALBERLA_MPI_SECTION()
         {
            mpi::allReduceInplace( pos[0], mpi::SUM );
            mpi::allReduceInplace( pos[1], mpi::SUM );
            mpi::allReduceInplace( pos[2], mpi::SUM );

            mpi::allReduceInplace( transVel[0], mpi::SUM );
            mpi::allReduceInplace( transVel[1], mpi::SUM );
            mpi::allReduceInplace( transVel[2], mpi::SUM );
         }

         // update position values
         posOld_ = posNew_;
         posNew_ = pos[2];

         if( fileIO_ )
            writeToFile( timestep, pos, transVel);
      }

      // return the relative change in the sphere position ( z coordinate )
      real_t getPositionDiff() const
      {
         return std::fabs( ( posNew_ - posOld_ ) / posNew_ );
      }

      // return the position (z coordinate)
      real_t getPosition() const
      {
         return posNew_;
      }

   private:
      void writeToFile( uint_t timestep, const pe::Vec3 & position, const pe::Vec3 & velocity )
      {
         WALBERLA_ROOT_SECTION()
         {
            std::ofstream file;
            file.open( filename_.c_str(), std::ofstream::app );
            file.setf( std::ios::unitbuf );
            file.precision(15);

            file << timestep << "\t" << position[0] << "\t" << position[1] << "\t" << position[2]
                             << "\t" << velocity[0] << "\t" << velocity[1] << "\t" << velocity[2]
                             << "\n";
            file.close();
         }
      }

      SweepTimeloop* timeloop_;

      Setup * setup_;

      shared_ptr< StructuredBlockStorage > blocks_;
      const BlockDataID bodyStorageID_;

      bool fileIO_;
      bool MO_CLI_;
      bool MO_MR_;
      bool eanReconstructor_;
      bool extReconstructor_;

      std::string filename_;

      real_t posOld_;
      real_t posNew_;
};

// When using N LBM steps and one (larger) pe step in a single simulation step, the force applied on the pe bodies in each LBM sep has to be scaled
// by a factor of 1/N before running the pe simulation. This corresponds to an averaging of the force and torque over the N LBM steps and is said to
// damp oscillations. Usually, N = 2.
// See Ladd - " Numerical simulations of particulate suspensions via a discretized Boltzmann equation. Part 1. Theoretical foundation", 1994, p. 302
class AverageForce
{
   public:
      AverageForce( const shared_ptr< StructuredBlockStorage > & blocks, const BlockDataID & bodyStorageID,
                    const uint_t lbmSubCycles )
      : blocks_( blocks ), bodyStorageID_( bodyStorageID ), invLbmSubCycles_( real_c(1) / real_c( lbmSubCycles ) ) { }

      void operator()()
      {
         pe::Vec3 force  = pe::Vec3(0.0);
         pe::Vec3 torque = pe::Vec3(0.0);

         for( auto blockIt = blocks_->begin(); blockIt != blocks_->end(); ++blockIt )
         {
            for( auto bodyIt = pe::BodyIterator::begin( *blockIt, bodyStorageID_); bodyIt != pe::BodyIterator::end(); ++bodyIt )
            {
               force  = invLbmSubCycles_ * bodyIt->getForce();
               torque = invLbmSubCycles_ * bodyIt->getTorque();

               bodyIt->resetForceAndTorque();

               bodyIt->setForce ( force );
               bodyIt->setTorque( torque );
            }
         }
      }
   private:
      shared_ptr< StructuredBlockStorage > blocks_;
      const BlockDataID bodyStorageID_;
      real_t invLbmSubCycles_;
};


// The flow in the channel is here driven by a body force and is periodic
// Thus, the pressure gradient, usually encountered in a channel flow, is not present here
// The buoyancy force on the body due to this pressure gradient has to be added 'artificially'
// F_{buoyancy} = - V_{body} * grad ( p ) = V_{body} * \rho_{fluid} *  a
// ( V_{body} := volume of body,  a := acceleration driving the flow )
class BuoyancyForce
{
   public:
   BuoyancyForce( const shared_ptr< StructuredBlockStorage > & blocks, const BlockDataID & bodyStorageID,
                  const Vector3<real_t> pressureForce )
      : blocks_( blocks ), bodyStorageID_( bodyStorageID ), pressureForce_( pressureForce ) { }

      void operator()()
      {

         for( auto blockIt = blocks_->begin(); blockIt != blocks_->end(); ++blockIt )
         {
            for( auto bodyIt = pe::LocalBodyIterator::begin( *blockIt, bodyStorageID_); bodyIt != pe::LocalBodyIterator::end(); ++bodyIt )
            {
               bodyIt->addForce ( pressureForce_ );
            }
         }
      }
   private:
      shared_ptr< StructuredBlockStorage > blocks_;
      const BlockDataID bodyStorageID_;
      const Vector3<real_t> pressureForce_;
};

class PDFFieldCopy
{
public:
   PDFFieldCopy( const BlockDataID & pdfFieldID, const BlockDataID & pdfFieldPreColID )
      : pdfFieldID_( pdfFieldID ), pdfFieldPreColID_( pdfFieldPreColID )
   {}
   void operator()( IBlock * block )
   {
      auto pdfField = block->getData< PdfField_T>( pdfFieldID_ );
      auto pdfFieldPreCol = block->getData< PdfField_T>( pdfFieldPreColID_ );
      std::copy( pdfField->data(), pdfField->data() + pdfField->allocSize(), pdfFieldPreCol->data() );
      pdfFieldPreCol->resetLatticeModel( pdfField->latticeModel() );

   }
private:
   BlockDataID pdfFieldID_;
   BlockDataID pdfFieldPreColID_;
};

//////////
// MAIN //
//////////

//*******************************************************************************************************************
/*!\brief Testcase that simulates a neutrally buoyant sphere in a Poiseuille flow
 *
 *   The Segre Silberberg effect denotes that a particle in a Poiseuille flow will eventually arrive
 *   at a distinct equilibrium position, depending on the particle size and its velocity.
 *      __________________
 * ->
 * -->        ___
 * --->      /   \
 * --->     |  x  |-\
 * --->      \___/   \-\
 * -->                  \------ equilibrium position
 * ->  ___________________
 *
 * The collision model used for the LBM is TRT with a relaxation parameter tau=1.4 and the magic parameter 3/16.
 * The particle is a sphere with diameter d=12 cells and density 1 (LU).
 * The domain is a channel with 4d x 4d x 4d, with walls in z-direction and periodicity else.
 * The simulation is run until the relative change in the z-coordinate between 100 time steps is less than 1e-5.
 *
 */
//*******************************************************************************************************************

int main( int argc, char **argv )
{
   debug::enterTestMode();

   mpi::Environment env( argc, argv );

   auto processes = MPIManager::instance()->numProcesses();

   if( processes != 1 && processes != 9 && processes != 18 )
   {
      std::cerr << "Number of processes must be equal to either 1, 9, or 18!" << std::endl;
      return EXIT_FAILURE;
   }

   ///////////////////
   // Customization //
   ///////////////////

   bool syncShadowOwners = false;
   bool shortrun         = false;
   bool functest         = false;
   bool fileIO           = false;
   bool vtkIO            = false;
   bool MO_CLI           = false; //use CLI boundary condition
   bool MO_MR            = false; //use multireflection boundary condition
   bool eanReconstructor = false; //use equilibrium and non-equilibrium reconstructor
   bool extReconstructor = false; //use extrapolation reconstructor

   for( int i = 1; i < argc; ++i )
   {
      if( std::strcmp( argv[i], "--syncShadowOwners" ) == 0 ) { syncShadowOwners = true; continue; }
      if( std::strcmp( argv[i], "--shortrun" )         == 0 ) { shortrun         = true; continue; }
      if( std::strcmp( argv[i], "--funcTest" )         == 0 ) { functest         = true; continue; }
      if( std::strcmp( argv[i], "--fileIO" )           == 0 ) { fileIO           = true; continue; }
      if( std::strcmp( argv[i], "--vtkIO" )            == 0 ) { vtkIO            = true; continue; }
      if( std::strcmp( argv[i], "--MO_CLI" )           == 0 ) { MO_CLI           = true; continue; }
      if( std::strcmp( argv[i], "--MO_MR"  )           == 0 ) { MO_MR            = true; continue; }
      if( std::strcmp( argv[i], "--eanReconstructor" ) == 0 ) { eanReconstructor = true; continue; }
      if( std::strcmp( argv[i], "--extReconstructor" ) == 0 ) { extReconstructor = true; continue; }
      WALBERLA_ABORT("Unrecognized command line argument found: " << argv[i]);
   }

   ///////////////////////////
   // SIMULATION PROPERTIES //
   ///////////////////////////

   Setup setup;

   setup.particleDiam =  real_c( 12 );                          // particle diameter
   setup.rho_p = real_c( 1 );                                   // particle density
   setup.tau = real_c( 1.4 );                                   // relaxation time
   setup.nu = (real_c(2) * setup.tau - real_c(1) ) / real_c(6); // viscosity in lattice units
   setup.timesteps = functest ? 1 : ( shortrun ? uint_c(200) : uint_c( 250000 ) ); // maximum number of time steps
   setup.zlength = uint_c( 4.0 * setup.particleDiam );          // zlength in lattice cells of the channel
   setup.xlength = setup.zlength;                               // xlength in lattice cells of the channel
   setup.ylength = setup.zlength;                               // ylength in lattice cells of the channel
   setup.Re = real_c( 13 );                                     // bulk Reynoldsnumber = uMean * width / nu

   const real_t particleRadius = real_c(0.5) * setup.particleDiam; // particle radius
   const real_t dx = real_c(1);                                    // lattice grid spacing
   const uint_t numLbmSubCycles = uint_c(2);                       // number of LBM subcycles, i.e. number of LBM steps until one pe step is carried out
   const real_t dt_pe       = real_c(numLbmSubCycles);             // time step size for PE (LU): here 2 times as large as for LBM
   const uint_t pe_interval = uint_c(1);                           // subcycling interval for PE: only 1 PE step
   const real_t omega = real_c(1) / setup.tau;                     // relaxation rate
   const real_t convergenceLimit = real_c( 1e-5 );                 // tolerance for relative change in position
   setup.checkFrequency = uint_c( real_c(100) / dt_pe );           // evaluate the position only every checkFrequency LBM time steps

   const real_t uMean = setup.Re * setup.nu / real_c( setup.zlength );
   const real_t uMax = real_c(2) * uMean;
   setup.forcing = uMax * ( real_c(2) * setup.nu ) / real_c( real_c(3./4.) * real_c(setup.zlength) * real_c(setup.zlength) ); // forcing to drive the flow
   setup.Re_p = uMean * particleRadius * particleRadius / ( setup.nu * real_c( setup.zlength ) );  // particle Reynolds number = uMean * r * r / ( nu * width )

   if( fileIO || vtkIO ){
      WALBERLA_ROOT_SECTION()
      {
         std::cout << "size: <" << setup.xlength << ", " << setup.ylength << ", " << setup.zlength << ">\n"
                   << "Re_p: " << setup.Re_p << ", Re_mean = " << setup.Re << "\n"
                   << "u_max: " << uMax << ", visc = " << setup.nu << "\n";
      }
   }

   ///////////////////////////
   // BLOCK STRUCTURE SETUP //
   ///////////////////////////

   setup.nBlocks[0] = uint_c(3);
   setup.nBlocks[1] = uint_c(3);
   setup.nBlocks[2] = (processes == 18) ? uint_c(2) : uint_c(1);
   setup.cellsPerBlock[0] = setup.xlength / setup.nBlocks[0];
   setup.cellsPerBlock[1] = setup.ylength / setup.nBlocks[1];
   setup.cellsPerBlock[2] = setup.zlength / setup.nBlocks[2];

   auto blocks = blockforest::createUniformBlockGrid( setup.nBlocks[0], setup.nBlocks[1], setup.nBlocks[2],
                                                      setup.cellsPerBlock[0], setup.cellsPerBlock[1], setup.cellsPerBlock[2], dx,
                                                      (processes != 1),
                                                      true, true, false );

   /////////////////
   // PE COUPLING //
   /////////////////

   // set up pe functionality
   shared_ptr<pe::BodyStorage>  globalBodyStorage = make_shared<pe::BodyStorage>();
   pe::SetBodyTypeIDs<BodyTypeTuple>::execute();

   auto bodyStorageID  = blocks->addBlockData(pe::createStorageDataHandling<BodyTypeTuple>(), "pe Body Storage");
   auto ccdID          = blocks->addBlockData(pe::ccd::createHashGridsDataHandling( globalBodyStorage, bodyStorageID ), "CCD");
   auto fcdID          = blocks->addBlockData(pe::fcd::createGenericFCDDataHandling<BodyTypeTuple, pe::fcd::AnalyticCollideFunctor>(), "FCD");

   // set up collision response, here DEM solver
   pe::cr::DEM cr(globalBodyStorage, blocks->getBlockStoragePointer(), bodyStorageID, ccdID, fcdID, NULL);

   // set up synchronization procedure
   const real_t overlap = real_c( 1.5 ) * dx;
   boost::function<void(void)> syncCall;
   if (!syncShadowOwners)
   {
      syncCall = boost::bind( pe::syncNextNeighbors<BodyTypeTuple>, boost::ref(blocks->getBlockForest()), bodyStorageID, static_cast<WcTimingTree*>(NULL), overlap, false );
   } else
   {
      syncCall = boost::bind( pe::syncShadowOwners<BodyTypeTuple>, boost::ref(blocks->getBlockForest()), bodyStorageID, static_cast<WcTimingTree*>(NULL), overlap, false );
   }

   // create pe bodies

   // bounding planes (global)
   const auto planeMaterial = pe::createMaterial( "myPlaneMat", real_c(8920), real_c(0), real_c(1), real_c(1), real_c(0), real_c(1), real_c(1), real_c(0), real_c(0) );
   pe::createPlane( *globalBodyStorage, 0, Vector3<real_t>(0,0,1), Vector3<real_t>(0,0,0), planeMaterial );
   pe::createPlane( *globalBodyStorage, 0, Vector3<real_t>(0,0,-1), Vector3<real_t>(0,0,real_c(setup.zlength)), planeMaterial );

   // add the sphere
   const auto sphereMaterial = pe::createMaterial( "mySphereMat", setup.rho_p , real_c(0.5), real_c(0.1), real_c(0.1), real_c(0.24), real_c(200), real_c(200), real_c(0), real_c(0) );
   Vector3<real_t> position( real_c(setup.xlength) * real_c(0.5), real_c(setup.ylength) * real_c(0.5), real_c(setup.zlength) * real_c(0.5) - real_c(1) );
   auto sphere = pe::createSphere( *globalBodyStorage, blocks->getBlockStorage(), bodyStorageID, 0, position, particleRadius, sphereMaterial );
   if( sphere != NULL )
   {
      real_t height = real_c( 0.5 ) * real_c( setup.zlength );
      // set sphere velocity to undisturbed fluid velocity at sphere center
      sphere->setLinearVel( setup.forcing/( real_c(2) * setup.nu) * ( height * height - ( position[2] - height ) * ( position[2] - height ) ), real_c(0), real_c(0) );
   }

   syncCall();

   ///////////////////////
   // ADD DATA TO BLOCKS //
   ////////////////////////

   // create the lattice model
   LatticeModel_T latticeModel = LatticeModel_T( lbm::collision_model::TRT::constructWithMagicNumber( omega ), SimpleConstant( Vector3<real_t> ( setup.forcing, real_c(0), real_c(0) ) ) );

   // add PDF field
   BlockDataID pdfFieldID = lbm::addPdfFieldToStorage< LatticeModel_T >( blocks, "pdf field (zyxf)", latticeModel,
                                                                         Vector3< real_t >( real_c(0), real_c(0), real_c(0) ), real_c(1),
                                                                         uint_t(1), field::zyxf );

   // add PDF field, for MR boundary condition only
   BlockDataID pdfFieldPreColID = lbm::addPdfFieldToStorage< LatticeModel_T >( blocks, " pre collision pdf field", latticeModel,
                                                                               Vector3< real_t >( real_c(0), real_c(0), real_c(0) ), real_c(1),
                                                                               uint_t(1), field::zyxf );

   // initialize already with the Poiseuille flow profile
   initPDF( blocks, pdfFieldID, setup);

   // add flag field
   BlockDataID flagFieldID = field::addFlagFieldToStorage<FlagField_T>( blocks, "flag field" );

   // add body field
   BlockDataID bodyFieldID = field::addToStorage<BodyField_T>( blocks, "body field", NULL, field::zyxf );

   // add boundary handling & initialize outer domain boundaries
   BlockDataID boundaryHandlingID = blocks->addStructuredBlockData< BoundaryHandling_T >(
                                    MyBoundaryHandling( flagFieldID, pdfFieldID, pdfFieldPreColID, bodyFieldID ), "boundary handling" );

   // map pe bodies into the LBM simulation
   if( MO_CLI )
   {
      // uses a higher order boundary condition (CLI)
      pe_coupling::mapMovingBodies< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID, MO_CLI_Flag );
   }else if ( MO_MR ){
      // uses a higher order boundary condition (MR)
      pe_coupling::mapMovingBodies< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID,  MO_MR_Flag );
   }else{
      // uses standard bounce back boundary conditions
      pe_coupling::mapMovingBodies< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID,  MO_BB_Flag );
   }

   ///////////////
   // TIME LOOP //
   ///////////////

   // setup of the LBM communication for synchronizing the pdf field between neighboring blocks
   boost::function< void () > commFunction;
   blockforest::communication::UniformBufferedScheme< Stencil_T > scheme( blocks );
   scheme.addPackInfo( make_shared< lbm::PdfFieldPackInfo< LatticeModel_T > >( pdfFieldID ) );
   commFunction = scheme;

   // create the timeloop
   SweepTimeloop timeloop( blocks->getBlockStorage(), setup.timesteps );

   // sweep for updating the pe body mapping into the LBM simulation
   if( MO_CLI )
   {
      timeloop.add()
         << Sweep( pe_coupling::BodyMapping< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID, MO_CLI_Flag, FormerMO_Flag ), "Body Mapping" );
   }else if ( MO_MR ){
      timeloop.add()
         << Sweep( pe_coupling::BodyMapping< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID,  MO_MR_Flag, FormerMO_Flag ), "Body Mapping" );
   }else{
      timeloop.add()
         << Sweep( pe_coupling::BodyMapping< BoundaryHandling_T >( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID,  MO_BB_Flag, FormerMO_Flag ), "Body Mapping" );
   }

   // sweep for restoring PDFs in cells previously occupied by pe bodies
   if( extReconstructor )
   {
      pe_coupling::FlagFieldNormalExtrapolationDirectionFinder< BoundaryHandling_T > extrapolationFinder( blocks, boundaryHandlingID );
      typedef pe_coupling::ExtrapolationReconstructor< LatticeModel_T, BoundaryHandling_T, pe_coupling::FlagFieldNormalExtrapolationDirectionFinder<BoundaryHandling_T> > Reconstructor_T;
      Reconstructor_T reconstructor( blocks, boundaryHandlingID, pdfFieldID, bodyFieldID, extrapolationFinder );
      timeloop.add()
         << Sweep( pe_coupling::PDFReconstruction< LatticeModel_T, BoundaryHandling_T, Reconstructor_T >
                      ( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID, reconstructor, FormerMO_Flag, Fluid_Flag  ), "PDF Restore" );
   } else if ( eanReconstructor )
   {
      pe_coupling::SphereNormalExtrapolationDirectionFinder extrapolationFinder( blocks, bodyFieldID );
      typedef pe_coupling::EquilibriumAndNonEquilibriumReconstructor< LatticeModel_T, BoundaryHandling_T, pe_coupling::SphereNormalExtrapolationDirectionFinder > Reconstructor_T;
      Reconstructor_T reconstructor( blocks, boundaryHandlingID, pdfFieldID, bodyFieldID, extrapolationFinder );
      timeloop.add()
         << Sweep( pe_coupling::PDFReconstruction< LatticeModel_T, BoundaryHandling_T, Reconstructor_T >
                      ( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID, reconstructor, FormerMO_Flag, Fluid_Flag  ), "PDF Restore" );
   } else {
      typedef pe_coupling::EquilibriumReconstructor< LatticeModel_T, BoundaryHandling_T > Reconstructor_T;
      Reconstructor_T reconstructor( blocks, boundaryHandlingID, pdfFieldID, bodyFieldID );
      timeloop.add()
         << Sweep( pe_coupling::PDFReconstruction< LatticeModel_T, BoundaryHandling_T, Reconstructor_T >
                      ( blocks, boundaryHandlingID, bodyStorageID, bodyFieldID, reconstructor, FormerMO_Flag, Fluid_Flag  ), "PDF Restore" );
   }

   for( uint_t lbmSubCycle = uint_c(0); lbmSubCycle < numLbmSubCycles; ++lbmSubCycle )
   {
      if( MO_MR )
      {
         // copy pdfs to have the pre collision PDFs available, needed for MR boundary treatment
         timeloop.add() << Sweep( PDFFieldCopy( pdfFieldID, pdfFieldPreColID ), "pdf field copy" );

         auto sweep = lbm::makeCellwiseSweep< LatticeModel_T, FlagField_T >( pdfFieldID, flagFieldID, Fluid_Flag );

         // collision sweep
         timeloop.add() << Sweep( lbm::makeCollideSweep( sweep ), "cell-wise LB sweep (collide)" );

         // add LBM communication function and boundary handling sweep (does the hydro force calculations and the no-slip treatment)
         timeloop.add() << BeforeFunction( commFunction, "LBM Communication" )
                        << Sweep( BoundaryHandling_T::getBlockSweep( boundaryHandlingID ), "Boundary Handling" );

         // streaming
         timeloop.add() << Sweep( lbm::makeStreamSweep( sweep ), "cell-wise LB sweep (stream)" );
      }
      else
      {
         // add LBM communication function and boundary handling sweep (does the hydro force calculations and the no-slip treatment)
         timeloop.add() << BeforeFunction( commFunction, "LBM Communication" )
                        << Sweep( BoundaryHandling_T::getBlockSweep( boundaryHandlingID ), "Boundary Handling" );

         // stream + collide LBM step
         timeloop.add()
            << Sweep( makeSharedSweep( lbm::makeCellwiseSweep< LatticeModel_T, FlagField_T >( pdfFieldID, flagFieldID, Fluid_Flag ) ), "cell-wise LB sweep" );
      }
   }

   // average the forces acting on the bodies over the number of LBM steps
   timeloop.addFuncAfterTimeStep( AverageForce( blocks, bodyStorageID, numLbmSubCycles ), "Force averaging for several LBM steps" );

   // add pressure force contribution
   timeloop.addFuncAfterTimeStep( BuoyancyForce( blocks, bodyStorageID,
                                                 Vector3<real_t>( math::M_PI / real_c(6) * setup.forcing * setup.particleDiam * setup.particleDiam * setup.particleDiam ,
                                                                  real_c(0), real_c(0) ) ),
                                  "Buoyancy force" );

   // add pe timesteps
   timeloop.addFuncAfterTimeStep( pe_coupling::TimeStep( blocks, bodyStorageID, cr, syncCall, dt_pe, pe_interval ), "pe Time Step" );

   // check for convergence of the particle position
   shared_ptr< SteadyStateCheck > check = make_shared< SteadyStateCheck >( &timeloop, &setup, blocks, bodyStorageID,
                                                                           fileIO, MO_CLI, MO_MR, eanReconstructor, extReconstructor );
   timeloop.addFuncAfterTimeStep( SharedFunctor< SteadyStateCheck >( check ), "steady state check" );

   if( vtkIO )
   {
      const uint_t writeFrequency = setup.checkFrequency;

      // spheres
      auto bodyVtkOutput   = make_shared<pe::DefaultBodyVTKOutput>( bodyStorageID, blocks->getBlockStorage() );
      auto bodyVTK   = vtk::createVTKOutput_PointData( bodyVtkOutput, "bodies", writeFrequency );
      timeloop.addFuncAfterTimeStep( vtk::writeFiles( bodyVTK ), "VTK (sphere data)" );

      // flag field (written only once in the first time step, ghost layers are also written)
      auto flagFieldVTK = vtk::createVTKOutput_BlockData( blocks, "flag_field", setup.timesteps, FieldGhostLayers );
      flagFieldVTK->addCellDataWriter( make_shared< field::VTKWriter< FlagField_T > >( flagFieldID, "FlagField" ) );
      timeloop.addFuncAfterTimeStep( vtk::writeFiles( flagFieldVTK ), "VTK (flag field data)" );

      // pdf field (ghost layers cannot be written because re-sampling/coarsening is applied)
      auto pdfFieldVTK = vtk::createVTKOutput_BlockData( blocks, "fluid_field", writeFrequency );
      pdfFieldVTK->setSamplingResolution( real_c(1) );

      blockforest::communication::UniformBufferedScheme< stencil::D3Q27 > pdfGhostLayerSync( blocks );
      pdfGhostLayerSync.addPackInfo( make_shared< field::communication::PackInfo< PdfField_T > >( pdfFieldID ) );
      pdfFieldVTK->addBeforeFunction( pdfGhostLayerSync );

      field::FlagFieldCellFilter< FlagField_T > fluidFilter( flagFieldID );
      fluidFilter.addFlag( Fluid_Flag );
      pdfFieldVTK->addCellInclusionFilter( fluidFilter );

      pdfFieldVTK->addCellDataWriter( make_shared< lbm::VelocityVTKWriter< LatticeModel_T, float > >( pdfFieldID, "VelocityFromPDF" ) );
      pdfFieldVTK->addCellDataWriter( make_shared< lbm::DensityVTKWriter < LatticeModel_T, float > >( pdfFieldID, "DensityFromPDF" ) );

      timeloop.addFuncAfterTimeStep( vtk::writeFiles( pdfFieldVTK ), "VTK (fluid field data)" );
   }


   timeloop.addFuncAfterTimeStep( RemainingTimeLogger( timeloop.getNrOfTimeSteps() ), "Remaining Time Logger" );

   ////////////////////////
   // EXECUTE SIMULATION //
   ////////////////////////

   WcTimingPool timeloopTiming;

   // time loop
   for (uint_t i = 0; i < setup.timesteps; ++i )
   {
      // perform a single simulation step
      timeloop.singleStep( timeloopTiming );

      // check if the relative change in the position is below the specified convergence criterion
      if ( i > setup.checkFrequency && check->getPositionDiff() < convergenceLimit )
      {
         // if simulation has converged, terminate simulation
         break;
      }
   }

   timeloopTiming.logResultOnRoot();

   // check the result
   if ( !functest && !shortrun )
   {
      // reference value from Inamuro et al. - "Flow between parallel walls containing the lines of neutrally buoyant circular cylinders" (2000), Table 2
      // note that this reference value provides only a rough estimate
      real_t referencePos = real_c( 0.2745 ) * real_c( setup.zlength );
      real_t relErr = std::fabs( referencePos - check->getPosition() ) / referencePos;
      if ( fileIO )
      {
         WALBERLA_ROOT_SECTION()
         {
            std::cout << "Expected position: " << referencePos << "\n"
                      << "Simulated position: " << check->getPosition() << "\n"
                      << "Relative error: " << relErr << "\n";
         }
      }
      // the relative error has to be below 10%
      WALBERLA_CHECK_LESS( relErr, real_c(0.1) );
   }

   return EXIT_SUCCESS;
}

} // namespace segre_silberberg_mem_pe

int main( int argc, char **argv ){
   segre_silberberg_mem_pe::main(argc, argv);
}
