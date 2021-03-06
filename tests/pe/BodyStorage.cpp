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
//! \file BodyStorage.cpp
//! \author Sebastian Eibl <sebastian.eibl@fau.de>
//
//======================================================================================================================

#include "pe/Materials.h"
#include "pe/rigidbody/Sphere.h"
#include "pe/Types.h"
#include "pe/rigidbody/BodyStorage.h"
#include "core/DataTypes.h"

#include "core/debug/TestSubsystem.h"

using namespace walberla::pe;

class Body1 : public Sphere {
public:
    static int refCount;
    Body1(walberla::id_t id, MaterialID matID) : Sphere(id, id, Vec3(0,0,0), Vec3(0,0,0), Quat(), 1, matID, false, true, false) {++refCount;}
    ~Body1() {--refCount;}
};

class Body2 : public Sphere {
public:
    static int refCount;
    Body2(walberla::id_t id, MaterialID matID) : Sphere(id, id, Vec3(0,0,0), Vec3(0,0,0), Quat(), 1, matID, false, true, false) {++refCount;}
    ~Body2() {--refCount;}
};

int Body1::refCount = 0;
int Body2::refCount = 0;

int main( int argc, char** argv )
{
    walberla::debug::enterTestMode();

    walberla::MPIManager::instance()->initializeMPI( &argc, &argv );

    MaterialID iron = Material::find("iron");
    {
        BodyStorage storage;
        auto bd1 = new Body1(1, iron);
        auto bd2 = new Body2(2, iron);
        auto bd3 = new Body2(3, iron);
        auto bd4 = new Body2(4, iron);

        WALBERLA_CHECK_EQUAL(Body1::refCount, 1);
        WALBERLA_CHECK_EQUAL(Body2::refCount, 3);

        storage.add(bd1);
        storage.add(bd2);
        storage.add(bd3);
        storage.add(bd4);

        WALBERLA_CHECK_EQUAL(storage.size(), 4);
        WALBERLA_CHECK_EQUAL(Body1::refCount, 1);
        WALBERLA_CHECK_EQUAL(Body2::refCount, 3);

        auto it = storage.find(bd3);
        WALBERLA_CHECK_EQUAL(it->getSystemID(), 3);
        storage.remove(it);
        WALBERLA_CHECK_EQUAL(storage.size(), 3);
        WALBERLA_CHECK_EQUAL(Body2::refCount, 2);

        auto it2 = storage.find(bd2->getSystemID());
        WALBERLA_CHECK_EQUAL(it2->getSystemID(), 2);
        storage.remove(it2);
        WALBERLA_CHECK_EQUAL(storage.size(), 2);
        WALBERLA_CHECK_EQUAL(Body2::refCount, 1);
    }

    WALBERLA_CHECK_EQUAL(Body1::refCount, 0);
    WALBERLA_CHECK_EQUAL(Body2::refCount, 0);
}
