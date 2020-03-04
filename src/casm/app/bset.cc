#include <cstring>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include "casm/app/casm_functions.hh"
#include "casm/app/AppIO.hh"
#include "casm/app/ProjectSettings.hh"
#include "casm/app/DirectoryStructure.hh"
#include "casm/casm_io/container/json_io.hh"
#include "casm/symmetry/SymGroup.hh"
#include "casm/clusterography/ClusterOrbits_impl.hh"
#include "casm/clusterography/IntegralCluster_impl.hh"
#include "casm/clusterography/ClusterSymCompare_impl.hh"
#include "casm/clex/PrimClex.hh"
#include "casm/clex/Clexulator.hh"
#include "casm/clex/ClexBasis.hh"
#include "casm/clex/ClexBasisWriter.hh"
#include "casm/clex/NeighborList.hh"
#include "casm/crystallography/Structure.hh"
#include "casm/completer/Handlers.hh"
#include "casm/database/DiffTransOrbitDatabase.hh"

namespace CASM {

  namespace Completer {

    BsetOption::BsetOption(): OptionHandlerBase("bset") {}

    void BsetOption::initialize() {
      add_help_suboption();

      m_desc.add_options()
      ("update,u", "Update basis set")
      ("orbits", "Pretty-print orbit prototypes")
      ("functions", "Pretty-print prototype cluster functions for each orbit")
      ("clusters", "Pretty-print all clusters")
      ("clex", po::value<std::string>()->value_name(ArgHandler::clex()), "Name of the cluster expansion using the basis set")
      ("force,f", "Force overwrite");
      return;
    }
  }

  // ///////////////////////////////////////
  // 'clusters' function for casm
  //    (add an 'if-else' statement in casm.cpp to call this)

  int bset_command(const CommandArgs &args) {

    po::variables_map vm;

    /// Set command line options using boost program_options
    Completer::BsetOption bset_opt;

    try {
      po::store(po::parse_command_line(args.argc(), args.argv(), bset_opt.desc()), vm); // can throw
      bool call_help = false;

      /** --help option
       */
      if(vm.count("help") || call_help) {
        args.log() << "\n";
        args.log() << bset_opt.desc() << std::endl;

        return 0;
      }

      if(vm.count("desc")) {
        args.log() << "\n";
        args.log() << bset_opt.desc() << std::endl;
        args.log() << "DESCRIPTION" << std::endl;
        args.log() << "    Generate and inspect cluster basis functions. A bspecs.json file should be available at\n"
                   << "        $ROOT/basis_set/$current_bset/bspecs.json\n"
                   << "    Run 'casm format --bspecs' for an example file.\n\n" ;

        return 0;
      }

      po::notify(vm); // throws on error, so do after help in case
      // there are any problems
    }
    catch(po::error &e) {
      args.err_log() << bset_opt.desc() << std::endl;
      args.err_log() << "\nERROR: " << e.what() << std::endl << std::endl;
      return ERR_INVALID_ARG;
    }
    catch(std::exception &e) {
      args.err_log() << bset_opt.desc() << std::endl;
      args.err_log() << "\nERROR: "  << e.what() << std::endl;
      return ERR_UNKNOWN;

    }

    const fs::path &root = args.root;
    if(root.empty()) {
      args.err_log().error("No casm project found");
      args.err_log() << std::endl;
      return ERR_NO_PROJ;
    }

    // If 'args.primclex', use that, else construct PrimClex in 'uniq_primclex'
    // Then whichever exists, store reference in 'primclex'
    std::unique_ptr<PrimClex> uniq_primclex;
    PrimClex &primclex = make_primclex_if_not(args, uniq_primclex);
    const DirectoryStructure &dir = primclex.dir();
    const ProjectSettings &set = primclex.settings();
    std::string bset;
    ClexDescription clex_desc;

    if(!vm.count("clex")) {
      clex_desc = set.default_clex();
    }
    else {
      auto it = set.cluster_expansions().find(vm["clex"].as<std::string>());
      if(it == set.cluster_expansions().end()) {
        args.err_log().error("Invalid --clex value");
        args.err_log() << vm["clex"].as<std::string>() << " not found.";
        return ERR_INVALID_ARG;
      }
      clex_desc = it->second;
    }
    bset = clex_desc.bset;

    if(vm.count("update")) {

      // initialize project info
      if(!fs::is_regular_file(dir.bspecs(bset))) {
        args.err_log().error("'bspecs.json' file not found");
        args.err_log() << "expected basis set specifications file at: " << dir.bspecs(bset) << "\n" << std::endl;
        return ERR_MISSING_INPUT_FILE;
      }


      std::vector<fs::path> filepaths({dir.clust(bset),
                                       dir.basis(bset),
                                       dir.clexulator_src(set.name(), bset),
                                       dir.clexulator_o(set.name(), bset),
                                       dir.clexulator_so(set.name(), bset)
                                      });

      bool any_existing_files = false;
      std::for_each(filepaths.cbegin(),
                    filepaths.cend(),
      [&](const fs::path & p) {
        if(fs::exists(p)) {
          if(!any_existing_files) {
            args.log().custom("Found existing files");
            any_existing_files = true;
          }
          args.log() << "found: " << p << "\n";
        }
      });

      if(any_existing_files) {
        if(vm.count("force")) {
          args.log() << "Using --force. Will overwrite existing files.\n" << std::endl;
          fs::remove(dir.clexulator_src(set.name(), bset));
          fs::remove(dir.clexulator_o(set.name(), bset));
          fs::remove(dir.clexulator_so(set.name(), bset));
          if(args.primclex) {
            args.primclex->refresh(false, false, false, false, true);
          }
        }
        else {
          args.log() << "Exiting due to existing files.  Use --force to force overwrite.\n" << std::endl;
          return ERR_EXISTING_FILE;
        }
      }

      jsonParser bspecs_json;
      jsonParser local_bspecs_json;
      std::unique_ptr<ClexBasis> clex_basis_ptr;

      std::vector<PrimPeriodicIntegralClusterOrbit> orbits;
      std::vector<LocalIntegralClusterOrbit> local_orbits;

      try {
        bspecs_json = jsonParser::parse(dir.bspecs(bset));

        if(bspecs_json.contains("local_bspecs")) {
          local_bspecs_json = bspecs_json["local_bspecs"];
          //this is a local basis set
          //get hop from bspecs
          args.log().construct("Orbitree");
          args.log() << std::endl;
          std::string orbitname = bspecs_json["diff_trans"].get<std::string>();
          PrimPeriodicDiffTransOrbit dtorbit = *primclex.db<PrimPeriodicDiffTransOrbit>().find(orbitname);

          const SymGroup &prim_grp = primclex.prim().factor_group();

          PrimPeriodicSymCompare<Kinetics::DiffusionTransformation> dt_sym_compare(primclex.shared_prim(), primclex.crystallography_tol());
          SymGroup generating_grp {
            make_invariant_subgroup(dtorbit.prototype(), prim_grp, dt_sym_compare)};

          make_local_orbits(
            dtorbit.prototype(),
            generating_grp,
            LocalSymCompare<IntegralCluster>(primclex.shared_prim(), primclex.crystallography_tol()),
            local_bspecs_json,
            alloy_sites_filter,
            primclex.crystallography_tol(),
            std::back_inserter(local_orbits),
            args.log());

          clex_basis_ptr.reset(new ClexBasis(primclex.shared_prim(), local_bspecs_json));
          clex_basis_ptr->generate(local_orbits.begin(), local_orbits.end(), local_bspecs_json);

        }
        else {
          args.log().construct("Orbitree");
          args.log() << std::endl;

          make_prim_periodic_orbits(
            primclex.shared_prim(),
            bspecs_json,
            alloy_sites_filter,
            set.crystallography_tol(),
            std::back_inserter(orbits),
            args.log());

          clex_basis_ptr.reset(new ClexBasis(primclex.shared_prim(), bspecs_json));
          clex_basis_ptr->generate(orbits.begin(), orbits.end(), bspecs_json);
        }

      }
      catch(std::exception &e) {
        args.err_log() << e.what() << std::endl;
        return ERR_INVALID_INPUT_FILE;
      }

      // -- write clust.json ----------------
      {
        jsonParser clust_json;
        if(bspecs_json.contains("local_bspecs")) {
          write_clust(local_orbits.begin(), local_orbits.end(), clust_json, ProtoSitesPrinter(), local_bspecs_json);
        }
        else {
          write_clust(orbits.begin(), orbits.end(), clust_json, ProtoSitesPrinter(), bspecs_json);
        }
        clust_json.write(dir.clust(bset));

        args.log().write(dir.clust(bset).string());
        args.log() << std::endl;
      }

      // -- write basis.json ----------------
      {
        jsonParser basis_json;
        if(bspecs_json.contains("local_bspecs")) {
          //throw std::runtime_error("No pretty printing of local cluster functions");
          // clex_basis should be replaced with local_clex_basisl
          write_site_basis_funcs(primclex.shared_prim(), *clex_basis_ptr, basis_json);
          write_clust(local_orbits.begin(), local_orbits.end(), basis_json, ProtoFuncsPrinter(*clex_basis_ptr, primclex.shared_prim()->shared_structure()), local_bspecs_json);
        }
        else {
          write_site_basis_funcs(primclex.shared_prim(), *clex_basis_ptr, basis_json);
          write_clust(orbits.begin(), orbits.end(), basis_json, ProtoFuncsPrinter(*clex_basis_ptr, primclex.shared_prim()->shared_structure()), bspecs_json);
        }
        basis_json.write(dir.basis(bset));

        args.log().write(dir.basis(bset).string());
        args.log() << std::endl;
      }


      // -- write global Clexulator
      if(bspecs_json.contains("local_bspecs")) {
        // get the neighbor list
        PrimNeighborList nlist(
          set.nlist_weight_matrix(),
          set.nlist_sublat_indices().begin(),
          set.nlist_sublat_indices().end()
        );

        // expand the nlist to contain sites in all orbits
        std::set<UnitCellCoord> nbors;
        local_neighborhood(local_orbits.begin(), local_orbits.end(), std::inserter(nbors, nbors.begin()));
        nlist.expand(nbors.begin(), nbors.end());

        // write source code
        fs::ofstream outfile;
        outfile.open(dir.clexulator_src(set.name(), bset));

        std::string parampack_type("DEFAULT");
        bspecs_json.get_if(parampack_type, "param_pack");
        ClexBasisWriter clexwriter(primclex.prim(), parampack_type);
        clexwriter.print_clexulator(set.global_clexulator_name(),
                                    *clex_basis_ptr,
                                    local_orbits,
                                    nlist,
                                    outfile,
                                    primclex.crystallography_tol());
        outfile.close();

        args.log().write(dir.clexulator_src(set.name(), bset).string());
        args.log() << std::endl;
      }
      else {
        // get the neighbor list
        PrimNeighborList nlist(
          set.nlist_weight_matrix(),
          set.nlist_sublat_indices().begin(),
          set.nlist_sublat_indices().end()
        );

        // expand the nlist to contain sites in all orbits
        std::set<UnitCellCoord> nbors;
        prim_periodic_neighborhood(orbits.begin(), orbits.end(), std::inserter(nbors, nbors.begin()));
        nlist.expand(nbors.begin(), nbors.end());

        // write source code
        fs::ofstream outfile;
        outfile.open(dir.clexulator_src(set.name(), bset));

        std::string parampack_type("DEFAULT");
        bspecs_json.get_if(parampack_type, "param_pack");
        ClexBasisWriter clexwriter(primclex.prim(), parampack_type);
        clexwriter.print_clexulator(set.global_clexulator_name(),
                                    *clex_basis_ptr,
                                    orbits,
                                    nlist,
                                    outfile,
                                    primclex.crystallography_tol());
        outfile.close();

        args.log().write(dir.clexulator_src(set.name(), bset).string());
        args.log() << std::endl;
      }

      // compile clexulator
      primclex.clexulator(set.default_clex());
    }
    else if(vm.count("orbits") || vm.count("clusters") || vm.count("functions")) {

      if(!fs::exists(dir.clust(bset))) {
        args.err_log().error("No 'clust.json' file found");
        args.err_log() << "Make sure to update your basis set with 'casm bset -u'.\n" << std::endl;
        return ERR_MISSING_DEPENDS;
      }

      typedef PrimPeriodicIntegralClusterOrbit orbit_type;
      typedef PrimPeriodicSymCompare<IntegralCluster> symcompare_type;

      std::vector<orbit_type> orbits;
      primclex.orbits(
        clex_desc,
        std::back_inserter(orbits),
        orbit_type::SymCompareType(primclex.shared_prim(), set.crystallography_tol()));

      if(vm.count("orbits")) {
        print_clust(orbits.begin(), orbits.end(), args.log(), ProtoSitesPrinter());
      }
      if(vm.count("clusters")) {
        print_clust(orbits.begin(), orbits.end(), args.log(), FullSitesPrinter());
      }
      if(vm.count("functions")) {
        print_site_basis_funcs(primclex.shared_prim(), primclex.clex_basis(clex_desc), args.log());
        print_clust(
          orbits.begin(),
          orbits.end(),
          args.log(),
          ProtoFuncsPrinter(primclex.clex_basis(clex_desc), primclex.shared_prim()->shared_structure()));
      }
    }
    else {
      args.err_log().error("Unknown error");
      args.err_log() << bset_opt.desc() << "\n" << std::endl;
    }

    return 0;
  };

}

