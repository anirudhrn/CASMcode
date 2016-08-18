#ifndef PRIMCLEX_HH
#define PRIMCLEX_HH

#include "casm/external/boost.hh"

#include "casm/misc/cloneable_ptr.hh"
#include "casm/casm_io/Log.hh"

#include "casm/crystallography/Structure.hh"
#include "casm/clex/CompositionConverter.hh"
#include "casm/clex/Supercell.hh"
#include "casm/clex/Clexulator.hh"
#include "casm/clex/ChemicalReference.hh"
#include "casm/clex/NeighborList.hh"
#include "casm/clex/ClexBasis.hh"

#include "casm/app/DirectoryStructure.hh"
#include "casm/app/ProjectSettings.hh"

/// Cluster expansion class
namespace CASM {

  class ECIContainer;

  template<typename T, typename U> class ConfigIterator;

  /// \defgroup Clex
  ///
  /// \brief A Configuration represents the values of all degrees of freedom in a Supercell
  ///


  /// \brief PrimClex stores the primitive Structure and lots of related data
  ///
  /// \ingroup Clex
  ///
  class PrimClex : public Logging {

    DirectoryStructure m_dir;
    ProjectSettings m_settings;

    Structure m_prim;
    bool m_vacancy_allowed;
    Index m_vacancy_index;

    /// Contains all the supercells that were involved in the enumeration.
    boost::container::stable_vector< Supercell > m_supercell_list;


    /// CompositionConverter specifies parameteric composition axes and converts between
    ///   parametric composition and mol composition
    bool m_has_composition_axes = false;
    CompositionConverter m_comp_converter;

    /// ChemicalReference specifies a reference for formation energies, chemical
    /// potentials, etc.
    notstd::cloneable_ptr<ChemicalReference> m_chem_ref;

    /// Stores the neighboring UnitCell and which sublattices to include in neighbor lists
    /// - mutable for lazy construction
    mutable notstd::cloneable_ptr<PrimNeighborList> m_nlist;


  public:

    typedef ConfigIterator<Configuration, PrimClex> config_iterator;
    typedef ConfigIterator<const Configuration, const PrimClex> config_const_iterator;

    // **** Constructors ****

    /// Initial construction of a PrimClex, from a primitive Structure
    PrimClex(const Structure &_prim, Log &log = default_log(), Log &debug_log = default_log(), Log &err_log = default_err_log());

    /// Construct PrimClex from existing CASM project directory
    ///  - read PrimClex and directory structure to generate all its Supercells and Configurations, etc.
    PrimClex(const fs::path &_root, Log &log = default_log(), Log &debug_log = default_log(), Log &err_log = default_err_log());

    /// Reload PrimClex data from settings
    void refresh(bool read_settings = false,
                 bool read_composition = false,
                 bool read_chem_ref = false,
                 bool read_configs = false,
                 bool clear_clex = false);

    // ** Directory path and settings accessors **

    const DirectoryStructure &dir() const {
      return m_dir;
    }

    ProjectSettings &settings() {
      return m_settings;
    }

    const ProjectSettings &settings() const {
      return m_settings;
    }



    // ** Composition accessors **

    /// check if CompositionConverter object initialized
    bool has_composition_axes() const;

    /// const Access CompositionConverter object
    const CompositionConverter &composition_axes() const;


    // ** Chemical reference **

    /// check if ChemicalReference object initialized
    bool has_chemical_reference() const;

    /// const Access ChemicalReference object
    const ChemicalReference &chemical_reference() const;


    // ** Accessors **

    /// const Access to primitive Structure
    const Structure &prim() const;

    ///Access to the primitive neighbor list
    PrimNeighborList &nlist() const;

    /// returns true if vacancy are an allowed species
    bool vacancy_allowed() const;

    /// returns the index of vacancies in composition vectors
    Index vacancy_index() const;


    // ** Supercell and Configuration accessors **

    /// const Access entire supercell_list
    const boost::container::stable_vector<Supercell> &supercell_list() const;

    /// const Access supercell by index
    const Supercell &supercell(Index i) const;

    /// Access supercell by index
    Supercell &supercell(Index i);

    /// const Access supercell by name
    const Supercell &supercell(std::string scellname) const;

    /// Access supercell by name
    Supercell &supercell(std::string scellname);

    /// access configuration by name (of the form "scellname/[NUMBER]", e.g., ("SCEL1_1_1_1_0_0_0/0")
    const Configuration &configuration(const std::string &configname) const;
    Configuration &configuration(const std::string &configname);

    /// Configuration iterator: begin
    config_iterator config_begin();

    /// Configuration iterator: end
    config_iterator config_end();

    /// Configuration iterator: begin
    config_const_iterator config_begin() const;

    /// Configuration iterator: end
    config_const_iterator config_end() const;

    /// const Configuration iterator: begin
    config_const_iterator config_cbegin() const;

    /// const Configuration iterator: end
    config_const_iterator config_cend() const;

    /// Configuration iterator: begin
    config_iterator selected_config_begin();

    /// Configuration iterator: end
    config_iterator selected_config_end();

    /// const Configuration iterator: begin
    config_const_iterator selected_config_cbegin() const;

    /// const Configuration iterator: end
    config_const_iterator selected_config_cend() const;


    // **** IO ****

    ///Call Configuration::write on every configuration to update files
    ///  - call update to also read all files
    void write_config_list();


    // **** Operators ****

    // **** Functions for preparing CLEXulators ****

    /// \brief Generate supercells of a certain volume and shape and store them in the array of supercells
    void generate_supercells(int volStart, int volEnd, int dims, const Eigen::Matrix3i &G, bool verbose);

    //Enumerate configurations for all the supercells that are stored in 'supercell_list'
    void print_enum_info(std::ostream &stream);
    void print_supercells() const;
    void print_supercells(std::ostream &stream) const;
    void read_supercells(std::istream &stream);
    void print_clex_configurations();


    //ParamComposition i/o and calculators in PrimClex

    void read_config_list();

    ///Fill up props of every configuration for a partucluar supercell. This will be deprecated when props disappears
    void read_scel_props(int scel_index, const std::string &JSON_output);
    ///Call read_config_props on every Supercell
    void read_all_scel_props(const std::string &JSON_output);

    ///Count over the number of configurations that are selected in all supercells
    int amount_selected() const;

    bool contains_supercell(std::string scellname, Index &index) const;

    Index add_supercell(const Lattice &superlat);

    Index add_canonical_supercell(const Lattice &superlat);


    bool has_clex_basis(const ClexDescription &key) const;
    const ClexBasis &clex_basis(const ClexDescription &key) const;

    bool has_clexulator(const ClexDescription &key) const;
    Clexulator clexulator(const ClexDescription &key) const;

    bool has_eci(const ClexDescription &key) const;
    const ECIContainer &eci(const ClexDescription &key) const;

  private:

    /// Initialization routines
    void _init();

    mutable std::map<ClexDescription, ClexBasis> m_clex_basis;
    mutable std::map<ClexDescription, Clexulator> m_clexulator;
    mutable std::map<ClexDescription, ECIContainer> m_eci;

  };


}
#endif
