#include "gtest/gtest.h"
#include "autotools.hh"
#include "Common.hh"

#include "casm/app/ProjectBuilder.hh"
#include "casm/app/ProjectSettings.hh"
#include "casm/clex/PrimClex.hh"
#include "casm/clex/ScelEnum.hh"
#include "casm/crystallography/Structure.hh"
#include "casm/crystallography/Superlattice.hh"
#include "casm/database/DatabaseTypes_impl.hh"
#include "casm/database/ScelDatabaseTools_impl.hh"

#include "crystallography/TestStructures.hh" // for test::ZrO_prim

// Enumerators
// -----------
//
// Enumerators in CASM are classes that provide iterators which when incremented iteratively
// constuct new objects, typically Supercell or Configuration. When used via the casm command line
// program subcommand `casm enum`, the constructed objects are added to a database for future use.
// When used in C++ code, the constructed objects can be stored in the database or the used in other
// ways.
//
// This example demonstrates enumerating Supercell. There are three related Supercell enumerators:
// - `ScelEnumByProps`: Enumerate Supercell by enumerating superlattices as specifying parameters
//   (CASM::xtal::ScelEnumProps) such as the beginning volume, ending volume, what the unit
//   lattice is (in terms of the prim lattice), and which lattice vectors to enumerate over. This
//   is similar to the example 002_crystallography_superlattice_test.cpp.

// This test fixture class constructs a CASM project for enumeration examples
class ExampleEnumerationZrOScelEnum : public testing::Test {
protected:

  std::string title;
  std::shared_ptr<CASM::Structure const> shared_prim;
  CASM::ProjectSettings project_settings;
  CASM::PrimClex primclex;

  // CASM::xtal::ScelEnumProps contains parameters which control which super lattice enumeration
  int begin_volume;
  int end_volume;
  std::string dirs;
  Eigen::Matrix3i generating_matrix;
  CASM::xtal::ScelEnumProps enumeration_params;

  ExampleEnumerationZrOScelEnum():
    title("ExampleEnumerationZrOScelEnum"),
    shared_prim(std::make_shared<CASM::Structure const>(test::ZrO_prim())),
    project_settings(make_default_project_settings(*shared_prim, title)),
    primclex(project_settings, shared_prim),
    begin_volume(1),
    end_volume(5),
    dirs("abc"),
    generating_matrix(Eigen::Matrix3i::Identity()),
    enumeration_params(begin_volume, end_volume, dirs, generating_matrix) {}

};

TEST_F(ExampleEnumerationZrOScelEnum, Example1) {

  // Enumerate supercells

  // The ScelEnumByProps enumerator provides iterators that construct Supercell as they are iterated
  CASM::ScelEnumByProps enumerator {shared_prim, enumeration_params};

  // The enumerator can be used to fill a container with Supercell
  std::vector<CASM::Supercell> supercells {enumerator.begin(), enumerator.end()};
  EXPECT_EQ(supercells.size(), 20);

  // The Database<Supercell> provides a way to store unique Supercell and save them to file
  // By convention, all objects in the database are in canonical form. This allows the database to
  // compare objects and keep only the unique ones.
  EXPECT_EQ(primclex.db<Supercell>().size(), 0);
  for(Supercell const &supercell : supercells) {

    // Supercell generated by ScelEnumByProps are in canonical form
    EXPECT_TRUE(supercell.is_canonical());

    // Enumerators may provide a specialization of `is_guaranteed_for_database_insert` which returns
    // true if enumerated objects are guaranteed ready for database insert.
    EXPECT_TRUE(is_guaranteed_for_database_insert(enumerator));

    // While supercells can be inserted directly, it is preferred to insert them via
    // `make_canonical_and_insert` to automate the canonicalization check. The overload which
    // accepts an enumerator as the first argument checks `is_guaranteed_for_database_insert` and
    // either inserts directly or makes canonical and then inserts.

    // insert directly (knowing supercell is canonical)
    // auto result = primclex.db<Supercell>().insert(supercell);

    // make canonical and then insert
    // auto result = make_canonical_and_insert(supercell, primclex.db<Supercell>());

    // prefer: check if can insert directly, else make canonical and then insert
    auto result = make_canonical_and_insert(enumerator, supercell, primclex.db<Supercell>());
  }

  EXPECT_EQ(primclex.db<Supercell>().size(), 20);
}
