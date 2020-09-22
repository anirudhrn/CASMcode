#ifndef CASM_enum_enumerate_configurations_impl
#define CASM_enum_enumerate_configurations_impl

#include "casm/app/enum/EnumInterface.hh"
#include "casm/app/enum/enumerate_configurations.hh"
#include "casm/clex/Supercell_impl.hh"
#include "casm/clex/Configuration_impl.hh"
#include "casm/database/ScelDatabase.hh"
#include "casm/database/ConfigDatabase.hh"
#include "casm/database/ConfigDatabaseTools.hh"

namespace CASM {

  /// Enumerate configurations
  ///
  /// This method enumerates configurations given a rage of starting values, typically a range
  /// of supercells or initial configurations, possibly with particular sites selected for
  /// enumeration, represented using `ConfigEnumInput`.
  ///
  /// A pseudo-code outline of the method is:
  /// \code
  /// for name_value_pair in range [name_value_pairs_begin, name_value_pairs_end):
  ///   print "Enumerate configuration for " + name_value_pair.first + "..."
  ///   auto enumerator = make_enumerator_f(name_value_pair.first, name_value_pair.second);
  ///   for configuration generated by enumerator:
  ///     if options.filter and !options.filter(configuration):
  ///       continue
  ///     if is_guaranteed_for_database_insert(enumerator):
  ///       insert configuration in configuration_db
  ///     else:
  ///       make primitive canonical configuration and insert in configuration_db
  ///       if !options.primitive_only:
  ///         make non-primitive canonical configuration and insert in configuration_db
  /// if !options.dry_run:
  ///   commit supercell_db
  ///   commit configuration_db
  /// \endcode
  ///
  /// Note:
  /// - To avoid unnecessary configuration canonicalization, specialize the following method:
  ///   `template<> bool is_guaranteed_for_database_insert(EnumeratorType const &);`
  ///
  ///
  /// \param options See EnumerateConfigurationsOptions for method options
  /// \param make_enumerator_f A unary functor that constructs a Configuration enumerator for each
  ///        value in the range `[name_value_pairs_begin, name_value_pairs_end)`.
  /// \param name_value_pairs_begin, name_value_pairs_end A range of pairs of name (for printing
  ///        progress) to `InputType` value (typically, but not necessarily
  ///        `InputType = ConfigEnumInput`). The value `InputType` is used as the argument to
  ///        `make_enumerator_f` to construct a series of enumerators which are executed in turn.
  /// \param supercell_db Will commit any new Supercell if `options.dry_run==false`.
  /// \param configuration_db Will commit any new Configuration if `options.dry_run==false`.
  /// \param logging For printing progress and errors
  ///
  template<typename MakeEnumeratorFunctor, typename InputNameValuePairIterator>
  void enumerate_configurations(
    EnumerateConfigurationsOptions const &options,
    MakeEnumeratorFunctor make_enumerator_f,
    InputNameValuePairIterator name_value_pairs_begin,
    InputNameValuePairIterator name_value_pairs_end,
    DB::Database<Supercell> &supercell_db,
    DB::Database<Configuration> &configuration_db,
    Logging const &logging) {

    Log &log = logging.log();
    std::pair<DB::Database<Configuration>::iterator, bool> insert_result;
    std::string dry_run_msg = CASM::dry_run_msg(options.dry_run);

    Index Ninit = configuration_db.size();
    log << dry_run_msg << "# configurations in this project: " << Ninit << "\n" << std::endl;

    log.set_verbosity(options.verbosity);
    log.begin<Log::standard>(options.method_name);

    auto it = name_value_pairs_begin;
    for(; it != name_value_pairs_end; ++it) {
      auto const &input_name_value_pair = *it;

      Index count = 0;
      Index count_filtered = 0;
      Index num_before = configuration_db.size();
      log << dry_run_msg << "Enumerate configurations for " << input_name_value_pair.first << " ...  " << std::flush;
      auto enumerator = make_enumerator_f(input_name_value_pair.first, input_name_value_pair.second);

      for(Configuration const &configuration : enumerator) {

        /// Use while transitioning Supercell to no longer need a `PrimClex const *`
        if(!configuration.supercell().has_primclex()) {
          configuration.supercell().set_primclex(options.primclex_ptr);
        }

        if(options.filter && !options.filter(configuration)) {
          ++count_filtered;
          continue;
        }
        ++count;

        if(is_guaranteed_for_database_insert(enumerator)) {
          configuration_db.insert(configuration);
        }
        else {
          make_canonical_and_insert(
            configuration,
            supercell_db,
            configuration_db,
            options.primitive_only);
        }
      }

      Index num_after = configuration_db.size();
      log << count << " configurations"
          << " (" << (num_after - num_before) << " new, "
          << count_filtered << " excluded by filter)." << std::endl;
    }
    log << dry_run_msg << "  DONE." << std::endl << std::endl;

    Index Nfinal = configuration_db.size();
    log << dry_run_msg << "# new configurations: " << Nfinal - Ninit << "\n";
    log << dry_run_msg << "# configurations in this project: " << Nfinal << "\n" << std::endl;

    if(!options.dry_run) {
      log << "Write supercell database..." << std::endl;
      supercell_db.commit();
      log << "  DONE" << std::endl << std::endl;

      log << "Write configuration database..." << std::endl;
      configuration_db.commit();
      log << "  DONE" << std::endl;
    }
    log.end_section();
  }

}

#endif
