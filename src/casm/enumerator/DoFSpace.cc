#include "casm/enumerator/DoFSpace.hh"
#include "casm/clex/Supercell.hh"
#include "casm/crystallography/Structure.hh"
#include "casm/symmetry/SupercellSymInfo_impl.hh"
#include "casm/symmetry/SymRepTools.hh"
namespace CASM {

  DoFSpace::DoFSpace(ConfigEnumInput _config_region,
                     DoFKey _dof_key,
                     Eigen::MatrixXd _dof_subspace) :
    config_region(std::move(_config_region)),
    dof_key(_dof_key) {
    Index dofdim = 0;
    if(AnisoValTraits(dof_key).global()) {
      dofdim = config_region.config().configdof().global_dof(dof_key).dim();
    }
    else if(dof_key == "occ") {
      std::vector<int> max_occ = config_region.supercell().max_allowed_occupation();
      for(Index i : config_region.sites()) {
        dofdim += max_occ[i] + 1;
      }
    }
    else {
      dofdim = config_region.config().configdof().local_dof(dof_key).dim() * config_region.sites().size();
    }

    if(dof_subspace.size() == 0)
      dof_subspace.setIdentity(dofdim, dofdim);
  }


  VectorSpaceSymReport vector_space_sym_report(DoFSpace const &_space,
                                               bool calc_wedges) {
    MasterSymGroup g;
    SymGroupRepID id;
    DoFKey dof_key = _space.dof_key;
    AnisoValTraits val_traits(dof_key);

    ConfigEnumInput const &config_region = _space.config_region;
    SupercellSymInfo const &sym_info = config_region.supercell().sym_info();
    xtal::BasicStructure const &prim_struc = config_region.config().prim().structure();

    std::vector<std::string> axis_glossary;

    if(prim_struc.global_dofs().count(dof_key)) {

      // Global DoF, use point group only
      SymGroup pointgroup = make_point_group(config_region.group(), sym_info.supercell_lattice());
      g = make_master_sym_group(pointgroup,
                                sym_info.supercell_lattice());

      id = g.allocate_representation();
      SymGroupRep const &rep = *(sym_info.global_dof_symrep(dof_key).rep_ptr());
      for(Index i = 0; i < pointgroup.size(); ++i) {
        Index fg_ix = pointgroup[i].index();
        g[i].set_rep(id, *rep[fg_ix]);
      }
      axis_glossary = component_descriptions(prim_struc.global_dof(dof_key));
    }
    else {
      auto group_and_ID = collective_dof_symrep(config_region.sites().begin(),
                                                config_region.sites().end(),
                                                sym_info,
                                                dof_key,
                                                config_region.group());
      g = group_and_ID.first;
      g.is_temporary_of(group_and_ID.first);
      id = group_and_ID.second;

      for(Index l : config_region.sites()) {
        Index b = config_region.config().sublat(l);
        if(!prim_struc.basis()[b].dofs().count(dof_key))
          continue;
        std::vector<std::string> tdescs = component_descriptions(prim_struc.basis()[b].dof(dof_key));
        for(std::string const &desc : tdescs) {
          axis_glossary.push_back(desc + "[" + std::to_string(l + 1) + "]");
        }
      }

    }

    SymGroupRep const &rep = g.representation(id);
    VectorSpaceSymReport result = vector_space_sym_report(rep,
                                                          g,
                                                          _space.dof_subspace,
                                                          calc_wedges);
    result.axis_glossary = std::move(axis_glossary);
    return result;

  }


}
