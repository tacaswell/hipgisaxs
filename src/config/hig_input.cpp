/**
 *  Project: HipGISAXS (High-Performance GISAXS)
 *
 *  File: hig_input.cpp
 *  Created: Jun 11, 2012
 *
 *  Author: Abhinav Sarje <asarje@lbl.gov>
 *
 *  Licensing: The HipGISAXS software is only available to be downloaded and
 *  used by employees of academic research institutions, not-for-profit
 *  research laboratories, or governmental research facilities. Please read the
 *  accompanying LICENSE file before downloading the software. By downloading
 *  the software, you are agreeing to be bound by the terms of this
 *  NON-COMMERCIAL END USER LICENSE AGREEMENT.
 */

#include <cfloat>

#include <config/hig_input.hpp>
#include <utils/utilities.hpp>
#include <utils/string_utils.hpp>
#include <common/parameters.hpp>
#include <file/hig_file_reader.hpp>


namespace hig {


  HiGInput::HiGInput() {
    InputReader::instance();
    TokenMapper::instance();
    HiGFileReader::instance();
  } // HiGInput::HiGInput()


  void HiGInput::init() {
    shapes_.clear();
    layers_.clear();
    structures_.clear();
    scattering_.init();
    detector_.init();
    compute_.init();
    struct_in_layer_ = false;

    shape_def_.clear();

    analysis_algos_.clear();
    param_key_map_.clear();
    param_space_key_map_.clear();
    param_data_key_map_.clear();

    curr_fit_param_.clear();
    curr_fit_algo_.clear();
    curr_fit_algo_param_.clear();

    // temp ...
    reference_data_.push_back(*(new FitReferenceData()));
    reference_data_set_ = false;
  } // init();


  bool HiGInput::construct_input_config(const char* filename) {
    if(!InputReader::instance().read_input(filename)) {
      std::cerr << "fatal error: some error happened in opening or reading "
            << "input config file. aborting"
            << std::endl;
      return false;
    } // if

    // make a pass and fill in with any include files
    curr_token_ = InputReader::instance().get_next_token();
    while(curr_token_.type_ != null_token) {
      if(curr_token_.type_ == error_token) {
        std::cerr << "aborting due to fatal error" << std::endl;
        return false;
      } // if
      if(curr_token_.type_ == include_token) {
        curr_token_ = InputReader::instance().get_next_token(); // assignement token
        curr_token_ = InputReader::instance().get_next_token(); // string token
        if(!InputReader::instance().read_include_file(curr_token_.svalue_)) return false;
      } // if
      curr_token_ = InputReader::instance().get_next_token();
    } // while

    InputReader::instance().rewind();

    curr_keyword_ = null_token; past_keyword_ = null_token;
    curr_token_ = InputReader::instance().get_next_token();
    past_token_.type_ = null_token;
    while(curr_token_.type_ != null_token) {
      if(curr_token_.type_ == error_token) {
        std::cerr << "aborting due to fatal error" << std::endl;
        return false;
      } // if
      if(!process_curr_token()) {
        std::cerr << "aborting due to fatal error" << std::endl;
        return false;
      } // if
      past_token_ = curr_token_;
      curr_token_ = InputReader::instance().get_next_token();
    } // while

    //print_all();

    return true;
  } // HiGInput::construct_input_config()


  bool HiGInput::process_curr_token() {
    TokenType parent = null_token;
    TokenType gparent = null_token;
    int dims;

    // process the token, do some basic syntax checking (improve with AST later) ...
    switch(curr_token_.type_) {

      case error_token:
        std::cerr << "aborting due to error" << std::endl;
        return false;

      case null_token:
        std::cerr << "error: something went wrong - should have already stopped!"
              << std::endl;
        return false;

      case white_space_token:
        std::cerr << "error: something went wrong - "
              << "seeing whitespace when not supposed to!" << std::endl;
        return false;

      case comment_token:  // do nothing
        return true;

      case object_begin_token:  // should be preceeded by '='
        if(past_token_.type_ != assignment_token && past_token_.type_ != comment_token) {
          std::cerr << "fatal error: unexpected object begin token '{'"
                << std::endl;
          return false;
        } // if
        keyword_stack_.push(curr_keyword_);
        break;

      case object_end_token:    // preceeded by number or string or '}'
        if(past_token_.type_ != number_token &&
            past_token_.type_ != string_token &&
            past_token_.type_ != object_end_token &&
            past_token_.type_ != array_end_token &&
            past_token_.type_ != object_begin_token &&
            past_token_.type_ != comment_token) {
          std::cerr << "fatal error: unexpected object close token '}'" << std::endl;
          return false;
        } // if
        if(keyword_stack_.size() < 1) {
          std::cerr << "fatal error: unexpected object close token '}'. "
                << "no matching object open token found" << std::endl;
          return false;
        } // if

        parent = get_curr_parent();
        switch(parent) {
          case shape_token:
            shapes_[curr_shape_.key()] = curr_shape_;  // insert the shape
            curr_shape_.clear();
            break;

          case shape_param_token:
            curr_shape_param_.set();
            //curr_shape_param_.print();
            curr_shape_.insert_param(curr_shape_param_.type_name(), curr_shape_param_);
            curr_shape_param_.clear();
            break;

          case refindex_token:
            // find out which ref index is this for
            gparent = get_curr_grandparent();
            switch(gparent) {
              case layer_token:  // nothing to do :-/ ??
                break;

              case shape_token:  // nothing to do :-/ ??
                break;

              default:
                std::cerr << "error: wrong place for a refindex" << std::endl;
                return false;
            } // switch
            break;

          case layer_token:      /* insert the current layer, and its key map */
            layers_[curr_layer_.order()] = curr_layer_;
            layer_key_map_[curr_layer_.key()] = curr_layer_.order();
            curr_layer_.clear();
            break;

          case unitcell_token:
            curr_unitcell_.element_list(curr_element_list_);
            unitcells_[curr_unitcell_.key()] = curr_unitcell_;
            curr_element_shape_key_.clear();
            curr_element_list_.clear();
            curr_unitcell_.clear();
            break;

          case unitcell_element_token:
            curr_element_shape_key_.clear();
            break;

          case struct_grain_token:
            switch(curr_structure_.getStructureType()){
              case paracrystal_type:
                dims = curr_structure_.paracrystal_getDimensions();
                if (dims != 1 && dims != 2){
                  std::cerr<< "Error: dimensions=" << dims << ". Should be either 1 or 2 for paracrystals" << std::endl;
                  return false;
                }
                break;
              case percusyevick_type:
                dims = curr_structure_.percusyevick_getDimensions();
                if (dims != 2 && dims != 3){
                  std::cerr<< "Error: \"dimensions\" can be either 2 or 3 for Percus-Yevick" << std::endl;
                  return false;
                }
            }
            break;
           
          case struct_token:      /* insert the current structure */
            /*
            switch(curr_structure_.getStructureType()){
              case paracrystal_type:
                if(!curr_structure_.paracrystal_sanity_check())
                  return false;
                break;
              case percusyevick_type:
                if(!curr_structure_.percusyevick_sanity_check())
                  return false;
                break;
            }
            */
            curr_structure_.construct_lattice_vectors();
            structures_[curr_structure_.key()] = curr_structure_;
            curr_structure_.clear();
            break;

          case struct_grain_lattice_token:  // nothing to do :-/
          case struct_grain_scaling_token: 
            break;


          case struct_grain_repetitiondist_token:  // nothing to do :-/
            // TODO: check if all three repetitions were defined
            // set flag that repetition is a dist
            curr_structure_.grain_is_repetition_dist(true);
            break;

          case struct_grain_xrepetition_token:
            // TODO: check for correctness of this repetition definition
            break;

          case struct_grain_yrepetition_token:
            // TODO: check for correctness of this repetition definition
            break;

          case struct_grain_zrepetition_token:
            // TODO: check for correctness of this repetition definition
            break;

          case struct_paracrystal_yspacing:
            break; // do nothing

          case struct_paracrystal_xspacing:
            if (curr_structure_.paracrystal_getDimensions() == 1){
              std::cerr << "Error: \"xspacing\" can\'t be used with 1D Paracrystals." << std::endl;
              return false;
            }
            break; // do nothing

          case struct_ensemble_orient_stat_token:  // nothing to do :-/
          case struct_ensemble_orient_rot1_token:  // nothing to do :-/
          case struct_ensemble_orient_rot2_token:  // nothing to do :-/
          case struct_ensemble_orient_rot3_token:  // nothing to do :-/
          case struct_ensemble_orient_token:  // nothing to do :-/
          case struct_ensemble_token:  // nothing to do :-/
          case instrument_scatter_alphai_token:  // nothing to do :-/
          case instrument_scatter_inplanerot_token:  // nothing to do :-/
          case instrument_scatter_tilt_token:  // nothing to do :-/
          case instrument_scatter_photon_token:  // nothing to do :-/
          case instrument_scatter_token:  // nothing to do :-/
          case instrument_detector_token:  // nothing to do :-/
          case instrument_token:  // nothing to do :-/
          case compute_outregion_token:  // nothing to do :-/
          case compute_token:  // nothing to do :-/
          case compute_structcorr_token:  // nothing to do :-/
          case compute_saveff_token:  // nothing to do :-/
          case compute_savesf_token:  // nothing to do :-/
          case hipgisaxs_token:  // nothing to do :-/
            break;

          case struct_grain_lattice_a_token:
          case struct_grain_lattice_b_token:
          case struct_grain_lattice_c_token:
            break;

          case fit_token:
          case fit_param_range_token:
            // ... // nothing to do :-/
            break;

          case fit_param_token:
            if(curr_fit_param_.key_.compare("") == 0 ||
                curr_fit_param_.variable_.compare("") == 0) {
              std::cerr << "error: incomplete fit parameter definition" << std::endl;
              return false;
            } // if
            if(param_key_map_.count(curr_fit_param_.key_) > 0 ||
                param_space_key_map_.count(curr_fit_param_.key_) > 0) {
              std::cerr << "error: duplicate key found in fit parameters" << std::endl;
              return false;
            } // if
            param_key_map_[curr_fit_param_.key_] = curr_fit_param_.variable_;
            param_space_key_map_[curr_fit_param_.key_] = curr_fit_param_.range_;
            param_data_key_map_[curr_fit_param_.key_] = curr_fit_param_;
            curr_fit_param_.clear();
            break;

          case fit_reference_data_token:      // nothing to do :-/
          case fit_reference_data_region_token:  // nothing to do :-/
          case fit_reference_data_npoints_token:  // nothing to do :-/
            // TODO: check for completeness and validity of the values ...
            // temp:
            reference_data_set_ = true;
            break;

          case fit_algorithm_token:
            analysis_algos_.push_back(curr_fit_algo_);
            curr_fit_algo_.clear();
            break;

          case fit_algorithm_param_token:
            curr_fit_algo_.add_param(curr_fit_algo_param_);
            curr_fit_algo_param_.clear();
            break;

          default:
            std::cerr << "error: something is wrong with one of your objects"
                  << std::endl;
                        std::cerr << "curr token type = " << curr_token_.type_ << std::endl;
                        std::cerr << "keyword = " << curr_keyword_ << ", parent = " << parent << std::endl;
            return false;
        } // switch
        if(keyword_stack_.size() < 1) {
          std::cerr << "something is really wrong. keyword_stack_ is empty when "
                   << "object end was found" << std::endl;
          return false;
        } // if
        past_keyword_ = curr_keyword_;
        curr_keyword_ = keyword_stack_.top();
        keyword_stack_.pop();
        break;

      case array_begin_token:  // should be preceeded by '=' OR '[' OR ']'
        if(past_token_.type_ != assignment_token &&
            past_token_.type_ != array_begin_token &&
            past_token_.type_ != array_end_token) {
          std::cerr << "fatal error: unexpected array begin token '['"
                << std::endl;
          return false;
        } // if
        if(past_token_.type_ == assignment_token)
          keyword_stack_.push(curr_keyword_);
        break;

      case array_end_token:  // preceeded by number_token or array_begin_token
        if(past_token_.type_ != number_token &&
            past_token_.type_ != array_begin_token &&
            past_token_.type_ != comment_token &&
            past_token_.type_ != array_end_token) {
          std::cerr << "fatal error: unexpected array close token ']'"
                << std::endl;
          return false;
        } // if
        if(keyword_stack_.size() < 1) {
          std::cerr << "fatal error: unexpected array close token ']', "
                << "no matching array open token found" << std::endl;
          return false;
        } // if

        parent = keyword_stack_.top();
        switch(parent) {
          case shape_originvec_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in originvec" << std::endl;
              return false;
            } // if
            curr_shape_.originvec(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;

          case unitcell_element_locations_token:
            if(past_token_.type_ != array_end_token) {
              if(curr_vector_.size() != 3) {
                std::cerr << "error: less than 3 values in unitcell element locations"
                          << std::endl;
                return false;
              } // if
              curr_element_list_[curr_element_shape_key_].push_back(vector3_t(curr_vector_));
              curr_vector_.clear();
            } else {
              if(curr_element_list_[curr_element_shape_key_].size() < 1 || curr_vector_.size() > 0) {
                std::cerr << "error: locations information is missing"
                          << std::endl;
                return false;
              } // if
              // nothing else to do
            } // if-else
            break;

          case struct_grain_lattice_a_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in lattice vector a"
                    << std::endl;
              return false;
            } // if
            curr_structure_.lattice_vec_a(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            curr_structure_.lattice_abc_set(true);
            break;

          case struct_grain_lattice_b_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in lattice vector b"
                    << std::endl;
              return false;
            } // if
            curr_structure_.lattice_vec_b(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            curr_structure_.lattice_abc_set(true);
            break;

          case struct_grain_lattice_c_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in lattice vector c"
                    << std::endl;
              return false;
            } // if
            curr_structure_.lattice_vec_c(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            curr_structure_.lattice_abc_set(true);
            break;

          case struct_grain_scaling_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error : scaling can be a scaler, vector[3] or a distribution." << std::endl;
              return false;
            } // if
            curr_structure_.grain_scaling_a_mean(curr_vector_[0]);
            curr_structure_.grain_scaling_b_mean(curr_vector_[1]);
            curr_structure_.grain_scaling_c_mean(curr_vector_[2]);
            break;

          case struct_grain_transvec_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in grain transvec"
                    << std::endl;
              return false;
            } // if
            curr_structure_.grain_transvec(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;

          case struct_grain_repetition_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in grain repetition"
                    << std::endl;
              return false;
            } // if
            curr_structure_.grain_repetition(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;

          case struct_ensemble_spacing_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in ensemble spacing"
                    << std::endl;
              return false;
            } // if
            curr_structure_.ensemble_spacing(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;

          case struct_ensemble_maxgrains_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: less than 3 values in ensemble maxgrains"
                    << std::endl;
              return false;
            } // if
            curr_structure_.ensemble_maxgrains(
                curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;

          case struct_ensemble_orient_rot_angles_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: values in orientation rotation angles should be 2"
                    << std::endl;
              return false;
            } // if
            // find out which rot is this for
            gparent = get_curr_grandparent();
            switch(gparent) {
              case struct_ensemble_orient_rot1_token:
                curr_structure_.grain_orientation_rot1_angles(
                    curr_vector_[0], curr_vector_[1]);
                break;
              case struct_ensemble_orient_rot2_token:
                curr_structure_.grain_orientation_rot2_angles(
                    curr_vector_[0], curr_vector_[1]);
                break;
              case struct_ensemble_orient_rot3_token:
                curr_structure_.grain_orientation_rot3_angles(
                    curr_vector_[0], curr_vector_[1]);
                break;
              default:
                std::cerr << "error: something wrong in the rot angles" << std::endl;
                return false;
            } // switch
            break;

          /*case instrument_scatter_smearing_token:
            if(curr_vector_.size() != 3) {
              std::cerr << "error: scattering smearing vector size should be 3"
                    << std::endl;
              return false;
            } // if
            scattering_.smearing(curr_vector_[0], curr_vector_[1], curr_vector_[2]);
            break;*/

          case instrument_detector_totpix_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: totalpixels vector size should be 2"
                    << std::endl;
              return false;
            } // if
            detector_.total_pixels(curr_vector_[0], curr_vector_[1]);
            break;

          case instrument_detector_dirbeam_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: detector direct beam vector size should be 2"
                    << std::endl;
              return false;
            } // if
            detector_.direct_beam(curr_vector_[0], curr_vector_[1]);
            break;

          case compute_resolution_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: resolution vector size should be 2"
                    << std::endl;
              return false;
            } // if
            compute_.resolution(curr_vector_[0], curr_vector_[1]);
            break;

          case compute_outregion_minpoint_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: output region min point vector size should be 2"
                    << std::endl;
              return false;
            } // if
            compute_.output_region_minpoint(curr_vector_[0], curr_vector_[1]);
            break;

          case compute_outregion_maxpoint_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: output region max point vector size should be 2"
                    << std::endl;
              return false;
            } // if
            compute_.output_region_maxpoint(curr_vector_[0], curr_vector_[1]);
            break;

          case fit_reference_data_region_min_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: reference data region min point vector size should be 2"
                    << std::endl;
              return false;
            } // if
            reference_data_[0].region_min(curr_vector_[0], curr_vector_[1]);
            break;

          case fit_reference_data_region_max_token:
            if(curr_vector_.size() != 2) {
              std::cerr << "error: reference data region max point vector size should be 2"
                    << std::endl;
              return false;
            } // if
            reference_data_[0].region_max(curr_vector_[0], curr_vector_[1]);
            break;

          default:
            std::cerr << "error: found array value in place of non-array type" << std::endl;
            return false;
        } // switch
        curr_vector_.clear();
        if(!(parent == unitcell_element_locations_token && past_token_.type_ == number_token)) {
          keyword_stack_.pop();
          past_keyword_ = curr_keyword_;
          curr_keyword_ = keyword_stack_.top();
        } else {
          // nothing?
        } // if-else
        break;

      case assignment_token:  // should be preceeded by a 'keyword',
                  // followed by '{' (object) or '[' (array)
                  // or string or number
        if(!preceeded_by_keyword()) {
          std::cerr << "error: misplaced assignment token '='" << std::endl;
          return false;
        } // if
        break;

      case number_token:    // preceeded by '=' or '[' or number_token
        if(past_token_.type_ != assignment_token &&
            past_token_.type_ != array_begin_token &&
            past_token_.type_ != number_token &&
            past_token_.type_ != comment_token &&
            past_token_.type_ != white_space_token) {
          std::cerr << "error: unexpected number '"
                << curr_token_.dvalue_ << "'" << std::endl;
          return false;
        } // if
        if(!process_number(curr_token_.dvalue_)) {
          std::cerr << "error: could not process number '"
                << curr_token_.dvalue_ << "'" << std::endl;
          return false;
        } // if
        break;

      case string_token:    // preceeded by '='
        if(past_token_.type_ != assignment_token &&
            past_token_.type_ != comment_token) {
          std::cerr << "error: stray string found '"
                << curr_token_.svalue_ << "'" << std::endl;
          return false;
        } // if
        if(!process_string(curr_token_.svalue_)) {
          std::cerr << "error: could not process string "
                << curr_token_.svalue_ << std::endl;
          return false;
        } // if
        break;

      case separator_token:  // should be preceeded by
                  // array_end or string or number or object_end
        if(past_token_.type_ != array_end_token &&
            past_token_.type_ != object_end_token &&
            past_token_.type_ != string_token &&
            past_token_.type_ != number_token &&
            past_token_.type_ != comment_token) {
          std::cerr << "error: stray seperator token ',' found" << std::endl;
          return false;
        } // if
        break;

      default:        // this is for keyword tokens
                  // read_oo_input makes sure there are no illegal tokens
                  // this is always preceeded by ',' or '{'
        if(curr_token_.type_ != hipgisaxs_token &&
            past_token_.type_ != object_begin_token &&
            past_token_.type_ != separator_token &&
            past_token_.type_ != comment_token) {
          std::cerr << "error: keyword '" << curr_token_.svalue_
                << "' not placed properly" << std::endl;
          return false;
        } // if
        past_keyword_ = curr_keyword_;
        curr_keyword_ = curr_token_.type_;
        if(!process_curr_keyword()) {
          std::cerr << "error: could not process current keyword '" << curr_token_.svalue_
                << "'" << std::endl;
          return false;
        } // if
        break;
    } // switch

    return true;
  } // HiGInput::process_curr_token()


  bool HiGInput::process_curr_keyword() {
    // do some syntax error checkings
    switch(curr_keyword_) {

      case hipgisaxs_token:  // this will always be the first token
        if(past_token_.type_ != null_token || keyword_stack_.size() != 0) {
          std::cerr << "fatal error: 'hipGisaxsInput' token is not at the beginning!"
                << std::endl;
          return false;
        } // if

        init();    // initialize everything
        break;

      case include_token:
        break;

      case key_token:
      case min_token:
      case max_token:
      case step_token:
      case rot_token:
      case type_token:
      case stat_token:
        break;

      case refindex_token:
      case refindex_delta_token:
      case refindex_beta_token:
        break;

      case shape_token:
        curr_shape_.init();
        break;

      case shape_name_token:
      case shape_originvec_token:
      case shape_zrot_token:
      case shape_yrot_token:
      case shape_xrot_token:
        break;

      case shape_param_token:
        curr_shape_param_.init();
        break;

      case shape_param_p1_token:
      case shape_param_p2_token:
      case shape_param_nvalues_token:
        break;

      case unitcell_token:
        curr_unitcell_.init();
        curr_element_list_.clear();
        curr_element_shape_key_.clear();
        break;

      case unitcell_element_token:
        curr_element_shape_key_.clear();
        break;

      case unitcell_element_skey_token:
        curr_element_shape_key_.clear();
        break;

      case unitcell_element_locations_token:
        break;

      case layer_token:
        curr_layer_.init();
        break;

      case layer_order_token:
      case layer_thickness_token:
        break;

      case struct_token:
        curr_structure_.init();
        break;

      case struct_dims:
      case struct_paracrystal_yspacing:
      case struct_paracrystal_xspacing:
      case struct_paracrystal_domain_size:
      case struct_percusyevick_volfract:
        break;

      case struct_iratio_token:
      case struct_grain_token:
      case struct_grain_ukey_token:
      case struct_grain_lkey_token:
      case struct_grain_lattice_token:
      case struct_grain_lattice_a_token:
      case struct_grain_lattice_b_token:
      case struct_grain_lattice_c_token:
      case struct_grain_lattice_hkl_token:
      case struct_grain_lattice_abangle_token:
      case struct_grain_lattice_caratio_token:
      case struct_grain_transvec_token:
      case struct_grain_scaling_token:
      case struct_grain_repetition_token:
      case struct_grain_repetitiondist_token:
      case struct_grain_xrepetition_token:
      case struct_grain_yrepetition_token:
      case struct_grain_zrepetition_token:
        break;

      case struct_ensemble_token:
      case struct_ensemble_spacing_token:
      case struct_ensemble_maxgrains_token:
      case struct_ensemble_distribution_token:
      case struct_ensemble_orient_token:
      case struct_ensemble_orient_stat_token:
      case struct_ensemble_orient_rot1_token:
      case struct_ensemble_orient_rot2_token:
      case struct_ensemble_orient_rot3_token:
      case struct_ensemble_orient_rot_axis_token:
      case struct_ensemble_orient_rot_angles_token:
      case struct_ensemble_orient_rot_anglelocation_token:
      case struct_ensemble_orient_rot_anglemean_token:
      case struct_ensemble_orient_rot_anglescale_token:
      case struct_ensemble_orient_rot_anglesd_token:
        break;

      case mean_token:
      case stddev_token:
      case nsamples_token:
        break;

      case compute_token:
      case compute_path_token:
      case compute_runname_token:
      case compute_method_token:
      case compute_resolution_token:
      case compute_nslices_token:
      case compute_outregion_token:
      case compute_outregion_maxpoint_token:
      case compute_outregion_minpoint_token:
      case compute_structcorr_token:
      case compute_palette_token:
      case compute_saveff_token:
      case compute_savesf_token:
        break;

      case instrument_token:
      case instrument_scatter_token:
      case instrument_scatter_expt_token:
      case instrument_scatter_alphai_token:
      case instrument_scatter_inplanerot_token:
      case instrument_scatter_tilt_token:
      case instrument_scatter_photon_token:
      case instrument_scatter_photon_value_token:
      case instrument_scatter_photon_unit_token:
      case instrument_scatter_polarize_token:
      case instrument_scatter_coherence_token:
      case instrument_scatter_spotarea_token:
      case instrument_scatter_smearing_token:
        break;

      case instrument_detector_token:
      case instrument_detector_origin_token:
      case instrument_detector_totpix_token:
      case instrument_detector_sdd_token:
      case instrument_detector_pixsize_token:
      case instrument_detector_dirbeam_token:
        break;

      case fit_token:
        break;

      case fit_param_token:
        curr_fit_param_.init();
        break;

      case fit_param_variable_token:
      case fit_param_range_token:
      case fit_param_init_token:
      case fit_reference_data_token:
      case fit_reference_data_path_token:
      case fit_reference_data_mask_token:
      case fit_reference_data_region_token:
      case fit_reference_data_region_min_token:
      case fit_reference_data_region_max_token:
      case fit_reference_data_npoints_token:
      case fit_reference_data_npoints_parallel_token:
      case fit_reference_data_npoints_perpendicular_token:
        break;

      case fit_algorithm_token:
        curr_fit_algo_.init();
        break;

      case fit_algorithm_name_token:
      case fit_algorithm_order_token:
        break;

      case fit_algorithm_param_token:
        curr_fit_algo_param_.init();
        break;

      case fit_algorithm_param_value_token:
      case fit_algorithm_restart_token:
      case fit_algorithm_tolerance_token:
      case fit_algorithm_regularization_token:
        break;

      case fit_algorithm_distance_metric_token:
        break;

      default:
        std::cerr << "error: non keyword token in keyword's position"
              << std::endl;
        return false;
    } // switch()

    return true;
  } // HiGInput::process_curr_keyword()


  inline TokenType HiGInput::get_curr_parent() {
    if(keyword_stack_.size() < 1) return null_token;
    return keyword_stack_.top();
  } // HiGInput::get_curr_parent()


  inline TokenType HiGInput::get_curr_grandparent() {
    if(keyword_stack_.size() < 1) return null_token;
    TokenType temp = keyword_stack_.top();
    keyword_stack_.pop();
    if(keyword_stack_.size() < 1) { keyword_stack_.push(temp); return null_token; }
    TokenType gparent = keyword_stack_.top();
    keyword_stack_.push(temp);
    return gparent;
  } // HiGInput::get_curr_grandparent()


  bool HiGInput::process_number(const real_t& num) {
    TokenType parent = null_token;
    TokenType gparent = null_token;

    switch(curr_keyword_) {
       
      case min_token:
        // find out which min is this for
        // shape param, scattering alphai, inplanerot, tilt
        parent = get_curr_parent();
        switch(parent) {
          case shape_param_token:
            curr_shape_param_.min(num);
            break;

          case struct_grain_xrepetition_token:
            curr_structure_.grain_xrepetition_min(num);
            break;

          case struct_grain_yrepetition_token:
            curr_structure_.grain_yrepetition_min(num);
            break;

          case struct_grain_zrepetition_token:
            curr_structure_.grain_zrepetition_min(num);
            break;

          case instrument_scatter_alphai_token:
            scattering_.alphai_min(num);
            break;

          case instrument_scatter_inplanerot_token:
            scattering_.inplane_rot_min(num);
            break;

          case instrument_scatter_tilt_token:
            scattering_.tilt_min(num);
            break;

          case fit_param_range_token:
            curr_fit_param_.range_.min_ = num;
            break;

          default:
            std::cerr << "'min' token appears in wrong place" << std::endl;
            return false;
        } // switch
        break;

      case max_token:
        // find out which max is this for
        // shape param, scattering alphai, inplanerot, tilt
        parent = get_curr_parent();
        switch(parent) {
          case shape_param_token:
            curr_shape_param_.max(num);
            break;

          case struct_grain_xrepetition_token:
            curr_structure_.grain_xrepetition_max(num);
            break;

          case struct_grain_yrepetition_token:
            curr_structure_.grain_yrepetition_max(num);
            break;

          case struct_grain_zrepetition_token:
            curr_structure_.grain_zrepetition_max(num);
            break;

          case instrument_scatter_alphai_token:
            scattering_.alphai_max(num);
            break;

          case instrument_scatter_inplanerot_token:
            scattering_.inplane_rot_max(num);
            break;

          case instrument_scatter_tilt_token:
            scattering_.tilt_max(num);
            break;

          case fit_param_range_token:
            curr_fit_param_.range_.max_ = num;
            break;

          default:
            std::cerr << "'max' token appears in wrong place" << std::endl;
            return false;
        } // switch
        break;

      case struct_grain_scaling_token:
        if(past_token_.type_ == assignment_token) {
          curr_structure_.grain_scaling_a_mean(num);
          curr_structure_.grain_scaling_b_mean(num);
          curr_structure_.grain_scaling_c_mean(num);
        } else {
          curr_vector_.push_back(num);
          if(curr_vector_.size() > 3) {
            std::cerr << "error: scaling can be a scalar, vector[3] or a distribution" << std::endl;
            return false;
          } // if
        } // if-else
        break;
      case step_token:
        // find out which step is this for
        // scattering alphai, inplanerot, tilt
        parent = get_curr_parent();
        switch(parent) {
          case instrument_scatter_alphai_token:
            scattering_.alphai_step(num);
          case instrument_scatter_inplanerot_token:
            scattering_.inplane_rot_step(num);
            break;

          case instrument_scatter_tilt_token:
            scattering_.tilt_step(num);
            break;

          case fit_param_range_token:
            curr_fit_param_.range_.step_ = num;
            break;

          default:
            std::cerr << "'step' token appears in a wrong place" << std::endl;
            return false;
        } // switch
        break;

      //case rot_token:
      //  break;

      case refindex_delta_token:
        // find out which ref index is this for
        // layer, grain
        parent = get_curr_parent();
        gparent = get_curr_grandparent();
        switch(gparent) {
          case layer_token:
            curr_layer_.refindex_delta(num);
            break;

          case shape_token:
            curr_shape_.refindex_delta(num);
            break;

          default:
            std::cerr << "'refindex' token appears in a wrong place" << std::endl;
            return false;
        } // switch
        break;

      case refindex_beta_token:
        // find out which ref index is this for
        // layer, grain
        parent = get_curr_parent();
        gparent = get_curr_grandparent();
        switch(gparent) {
          case layer_token:
            curr_layer_.refindex_beta(num);
            break;

          case shape_token:
            curr_shape_.refindex_beta(num);
            break;

          default:
            std::cerr << "'refindex' token appears in a wrong place" << std::endl;
            return false;
        } // switch
        break;


      case shape_originvec_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in origin vector" << std::endl;
          return false;
        } // if
        break;

      case shape_zrot_token:
        curr_shape_.zrot(num * PI_ / 180.);
        break;

      case shape_yrot_token:
        curr_shape_.yrot(num * PI_ / 180.);
        break;

      case shape_xrot_token:
        curr_shape_.xrot(num * PI_ / 180.);
        break;

      case shape_param_p1_token:
        curr_shape_param_.p1(num);
        break;

      case shape_param_p2_token:
        curr_shape_param_.p2(num);
        break;

      case shape_param_nvalues_token:
        curr_shape_param_.nvalues(num);
        break;

      case unitcell_element_locations_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in a locations vector" << std::endl;
          return false;
        } // if
        break;

      case layer_order_token:
        curr_layer_.order(num);
        break;

      case layer_thickness_token:
        curr_layer_.thickness(num);
        break;

      case struct_dims:
        switch (curr_structure_.getStructureType()){
          case paracrystal_type:
            curr_structure_.paracrystal_putDimensions(num);
            break;
          case percusyevick_type:
            curr_structure_.percusyevick_putDimensions(num);
            break;
        }
        break;
      
      case struct_paracrystal_domain_size:
        curr_structure_.paracrystal_putDomainSize(num);
        break;

      case struct_percusyevick_volfract:
        curr_structure_.percusyevick_putVolf(num);

      case struct_iratio_token:
        if (num <= 0) {
          std::cerr << "error: iratio can't be a negative number or zeros" << std::endl;
          return false;
        }
        curr_structure_.iratio(num);
        break;

      case struct_grain_lattice_a_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in lattice vector a" << std::endl;
          return false;
        } // if
        break;

      case struct_grain_lattice_b_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in lattice vector b" << std::endl;
          return false;
        } // if
        break;

      case struct_grain_lattice_c_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in lattice vector c" << std::endl;
          return false;
        } // if
        break;

      case struct_grain_lattice_abangle_token:
        curr_structure_.lattice_abangle(num);
        break;
        
      case struct_grain_lattice_caratio_token:
        curr_structure_.lattice_caratio(num);
        break;
        
      case struct_grain_transvec_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in trans vector" << std::endl;
          return false;
        } // if
        break;

      case mean_token:
        switch (get_curr_parent()) {
          case struct_grain_lattice_a_token:
            curr_structure_.grain_scaling_a_mean(num);
            break;
          case struct_grain_lattice_b_token:
            curr_structure_.grain_scaling_b_mean(num);
            break;
          case struct_grain_lattice_c_token:
            curr_structure_.grain_scaling_c_mean(num);
            break;
          case struct_paracrystal_xspacing:
            curr_structure_.paracrystal_putDistXMean(num);
            break;
          case struct_paracrystal_yspacing:
            curr_structure_.paracrystal_putDistYMean(num);
            break;
          default:
            std::cerr <<"error:distribution is not implemented for this type yet" << std::endl;
            return false;
        }
        break;

      case stddev_token:
        switch (get_curr_parent()) {
          case struct_grain_lattice_a_token:
            curr_structure_.grain_scaling_a_stddev(num);
            break;
          case struct_grain_lattice_b_token:
            curr_structure_.grain_scaling_b_stddev(num);
            break;
          case struct_grain_lattice_c_token:
            curr_structure_.grain_scaling_c_stddev(num);
            break;
          case struct_paracrystal_xspacing:
            curr_structure_.paracrystal_putDistXStddev(num);
            break;
          case struct_paracrystal_yspacing:
            curr_structure_.paracrystal_putDistYStddev(num);
            break;
          default:
            std::cerr <<"error:distribution is not implemented for this type yet" << std::endl;
            return false;
        }
        break;

      case nsamples_token:
        switch (get_curr_parent()){
          case struct_grain_lattice_a_token:
            curr_structure_.grain_scaling_a_nsamples(num);
            break;
          case struct_grain_lattice_b_token:
            curr_structure_.grain_scaling_b_nsamples(num);
            break;
          case struct_grain_lattice_c_token:
            curr_structure_.grain_scaling_c_nsamples(num);
            break;
        }
        break;

      case struct_grain_repetition_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in repetition vector" << std::endl;
          return false;
        } // if
        break;

      case struct_ensemble_spacing_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in spacing vector" << std::endl;
          return false;
        } // if
        break;

      case struct_ensemble_maxgrains_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 3) {
          std::cerr << "error: more than 3 values in maxgrains vector" << std::endl;
          return false;
        } // if
        break;

      case struct_ensemble_orient_rot_angles_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in angles vector" << std::endl;
          return false;
        } // if
        break;

      case struct_ensemble_orient_rot_anglelocation_token:
        parent = get_curr_parent();
        switch(parent) {
          case struct_ensemble_orient_rot1_token:
            curr_structure_.grain_orientation_rot1_anglelocation(num);
            break;
          case struct_ensemble_orient_rot2_token:
            curr_structure_.grain_orientation_rot2_anglelocation(num);
            break;
          case struct_ensemble_orient_rot3_token:
            curr_structure_.grain_orientation_rot3_anglelocation(num);
            break;
          default:
            std::cerr << "error: something wrong in the rot angle location" << std::endl;
            return false;
        } // switch
        break;

      case struct_ensemble_orient_rot_anglemean_token:
        parent = get_curr_parent();
        switch(parent) {
          case struct_ensemble_orient_rot1_token:
            curr_structure_.grain_orientation_rot1_anglemean(num);
            break;
          case struct_ensemble_orient_rot2_token:
            curr_structure_.grain_orientation_rot2_anglemean(num);
            break;
          case struct_ensemble_orient_rot3_token:
            curr_structure_.grain_orientation_rot3_anglemean(num);
            break;
          default:
            std::cerr << "error: something wrong in the rot angle mean" << std::endl;
            return false;
        } // switch
        break;

      case struct_ensemble_orient_rot_anglescale_token:
        parent = get_curr_parent();
        switch(parent) {
          case struct_ensemble_orient_rot1_token:
            curr_structure_.grain_orientation_rot1_anglescale(num);
            break;
          case struct_ensemble_orient_rot2_token:
            curr_structure_.grain_orientation_rot2_anglescale(num);
            break;
          case struct_ensemble_orient_rot3_token:
            curr_structure_.grain_orientation_rot3_anglescale(num);
            break;
          default:
            std::cerr << "error: something wrong in the rot angle scale" << std::endl;
            return false;
        } // switch
        break;

      case struct_ensemble_orient_rot_anglesd_token:
        parent = get_curr_parent();
        switch(parent) {
          case struct_ensemble_orient_rot1_token:
            curr_structure_.grain_orientation_rot1_anglesd(num);
            break;
          case struct_ensemble_orient_rot2_token:
            curr_structure_.grain_orientation_rot2_anglesd(num);
            break;
          case struct_ensemble_orient_rot3_token:
            curr_structure_.grain_orientation_rot3_anglesd(num);
            break;
          default:
            std::cerr << "error: something wrong in the rot angle sd" << std::endl;
            return false;
        } // switch
        break;

      case compute_outregion_maxpoint_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in maxpoint" << std::endl;
          return false;
        } // if
        break;

      case compute_outregion_minpoint_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in minpoint" << std::endl;
          return false;
        } // if
        break;

      case compute_resolution_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in resolution vector" << std::endl;
          return false;
        } // if
        break;

      case compute_nslices_token:
        compute_.nslices(num);
        break;


      case instrument_scatter_photon_value_token:
        scattering_.photon_value(num);
        break;

      case instrument_scatter_coherence_token:
        scattering_.coherence(num);
        break;

      case instrument_scatter_spotarea_token:
        scattering_.spot_area(num);
        break;

      case instrument_scatter_smearing_token:
        //curr_vector_.push_back(num);
        //if(curr_vector_.size() > 3) {
        //  std::cerr << "error: more than 3 values in scatter smearing" << std::endl;
        //  return false;
        //} // if
        scattering_.smearing(num);
        break;

      case instrument_detector_totpix_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in totalpixels vector" << std::endl;
          return false;
        } // if
        break;

      case instrument_detector_sdd_token:
        detector_.sd_distance(num);
        break;

      case instrument_detector_pixsize_token:
        detector_.pixel_size(num);
        break;

      case instrument_detector_dirbeam_token:
        curr_vector_.push_back(num);
        if(curr_vector_.size() > 2) {
          std::cerr << "error: more than 2 values in directbeam vector" << std::endl;
          return false;
        } // if
        break;

      case fit_param_init_token:
        curr_fit_param_.init_ = num;
        break;

      case fit_reference_data_region_min_token:
        curr_vector_.push_back(num);
        break;

      case fit_reference_data_region_max_token:
        curr_vector_.push_back(num);
        break;

      case fit_reference_data_npoints_parallel_token:
        reference_data_[0].npoints_parallel(num);
        break;

      case fit_reference_data_npoints_perpendicular_token:
        reference_data_[0].npoints_perpendicular(num);
        break;

      case fit_algorithm_order_token:
        curr_fit_algo_.order(num);
        break;

      case fit_algorithm_param_value_token:
        curr_fit_algo_param_.value(num);
        break;

      case fit_algorithm_tolerance_token:
        curr_fit_algo_.tolerance(num);
        break;

      case fit_algorithm_regularization_token:
        curr_fit_algo_.regularization(num);
        break;

      default:
        std::cerr << "fatal error: found a number '" << num
              << "' where it should not be [" << curr_keyword_ << "]" << std::endl;
        return false;
    } // switch

    return true;
  } // HiGInput::process_number()


  bool HiGInput::process_string(const std::string& str) {
    TokenType parent = null_token;
    ShapeName shp = shape_null;

    switch(curr_keyword_) {
      case include_token:
        if(!InputReader::instance().read_include_file(str)) {
          std::cerr << "fatal error: some error happened in opening or reading "
                    << "include config file " << str << ". aborting"
                    << std::endl;
          return false;
        } // if
        break;

      case key_token:
        // find out which key is this for
        parent = get_curr_parent();
        switch(parent) {
          case shape_token:
            curr_shape_.key(str);
            break;

          case layer_token:
            curr_layer_.key(str);
            break;

          case unitcell_token:
            curr_unitcell_.key(str);
            break;

          case struct_token:
            curr_structure_.key(str);
            break;

          case fit_param_token:
            curr_fit_param_.key_ = str;
            break;

          default:
            std::cerr << "error: extraneous key" << std::endl;
            return false;
        } // switch
        break;

      case shape_name_token:
        shp = TokenMapper::instance().get_shapename_token(str);
        if(shp == shape_error) {
          std::cerr << "error: shape name '" << str
                << "' is an unknown shape, and is not a shape file" << std::endl;
          return false;
        } // if
        curr_shape_.name_str(str);
        curr_shape_.name(shp);
        break;

      case type_token:
        // find out which type is this for
        parent = get_curr_parent();
        switch(parent) {
          case shape_param_token:
            curr_shape_param_.type(TokenMapper::instance().get_shapeparam_token(str));
            curr_shape_param_.type_name(str);
            break;

          case struct_grain_lattice_token:
            curr_structure_.lattice_type(
                TokenMapper::instance().get_lattice_type(str));
            break;

          case compute_outregion_token:
            compute_.output_region_type(
                TokenMapper::instance().get_output_region_type(str));
            break;

          case fit_reference_data_region_token:
            reference_data_[0].region_type(
                TokenMapper::instance().get_reference_data_region_type(str));
            break;

          case fit_algorithm_param_token:
            curr_fit_algo_param_.type(
                TokenMapper::instance().get_fit_algorithm_param_token(str));
            curr_fit_algo_param_.type_name(str);
            break;

          case struct_grain_token:
            switch(TokenMapper::instance().get_keyword_token(str)){
              case struct_paracrystal:
                curr_structure_.paracrystal_init();
                break;
              case struct_percusyevick:
                curr_structure_.percusyevick_init();
                if(shapes_.size() == 1){
                  shape_iterator_t s = shapes_.begin(); 
                  if(s->second.name() != shape_sphere){
                    std::cerr << "Error: Percus-Yevick is defined for hard spheres only" << std::endl;
                    return false;
                  }
                  // get diameter
                  shape_param_list_t sp_list = s->second.param_list();
                  // it should only have radius nothing else
                  curr_structure_.percusyevick_putDiameter(2. * sp_list["radius"].min());
                } else if (shapes_.size() > 1){
                  Unitcell::element_iterator_t i_el = 
                      unitcells_[curr_structure_.grain_unitcell_key()].element_end();
                  if (i_el != unitcells_[curr_structure_.grain_unitcell_key()].element_end()){
                    std::cerr << "Unit cell in Percus-Yevick can't have more than one shape" << std::endl;
                    return false;
                  }
                  shape_param_list_t sp_list = shapes_[i_el->first].param_list();
                  curr_structure_.percusyevick_putDiameter(2. * sp_list["radius"].min());
                } else {
                    std::cerr << "error: something wrong here!" << std::endl;
                    return false;
                }
                break;
            }
            break;

          case struct_token:
            break;

          default:
            std::cerr << "error: 'type' token in wrong place" << std::endl;
            return false;
        } // switch
        break;

      case stat_token:
        // find out which stat is this for
        parent = get_curr_parent();
        switch(parent) {
          case shape_param_token:
            curr_shape_param_.stat(TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_grain_xrepetition_token:
            curr_structure_.grain_xrepetition_stat(TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_grain_yrepetition_token:
            curr_structure_.grain_yrepetition_stat(TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_grain_zrepetition_token:
            curr_structure_.grain_zrepetition_stat(TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_ensemble_orient_token:
            curr_structure_.ensemble_orientation_stat(str);
            break;

          case struct_grain_lattice_a_token:
            curr_structure_.grain_scaling_a_stat (TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_grain_lattice_b_token:
            curr_structure_.grain_scaling_b_stat (TokenMapper::instance().get_stattype_token(str));
            break;

          case struct_grain_lattice_c_token:
            curr_structure_.grain_scaling_c_stat (TokenMapper::instance().get_stattype_token(str));
            break;

          default:
            std::cerr << "error: 'stat' token in wrong place" << std::endl;
            return false;
        } // switch
        break;

      case unitcell_element_skey_token:
        curr_element_list_[str].clear();  // initiaize
        curr_element_shape_key_ = str;
        break;

      //case struct_grain_skey_token:
      case struct_grain_ukey_token:
        //curr_structure_.grain_shape_key(str);
        curr_structure_.grain_unitcell_key(str);
        break;

      case struct_grain_lkey_token:
        curr_structure_.grain_layer_key(str);
        struct_in_layer_ = true;
        break;

      case struct_ensemble_distribution_token:
        curr_structure_.ensemble_distribution(str);
        break;

      case struct_ensemble_orient_rot_axis_token:
        // find out which of the 3 rot is this for
        parent = get_curr_parent();
        switch(parent) {
          case struct_ensemble_orient_rot1_token:
            curr_structure_.grain_orientation_rot1_axis(str.c_str()[0]);
            break;

          case struct_ensemble_orient_rot2_token:
            curr_structure_.grain_orientation_rot2_axis(str.c_str()[0]);
            break;

          case struct_ensemble_orient_rot3_token:
            curr_structure_.grain_orientation_rot3_axis(str.c_str()[0]);
            break;

          default:
            std::cerr << "error: 'axis' token in wrong place" << std::endl;
            return false;
        } // switch
        break;

      case struct_grain_lattice_token:
        curr_structure_.lattice_type(
            TokenMapper::instance().get_lattice_type(str));
        break;
 
      case struct_grain_lattice_hkl_token:
        curr_structure_.lattice_hkl(str);
        break;

      case instrument_scatter_expt_token:
        scattering_.expt(str);
        break;

      case instrument_scatter_photon_unit_token:
        scattering_.photon_unit(str);
        break;

      case instrument_scatter_polarize_token:
        scattering_.polarization(str);
        break;

      case instrument_detector_origin_token:
        detector_.origin(str);
        break;

      case compute_path_token:
        compute_.pathprefix(str);
        break;

      case compute_runname_token:
        compute_.runname(str);
        break;

      case compute_method_token:
        compute_.method(str);
        break;

      case compute_structcorr_token:
        compute_.structcorrelation(TokenMapper::instance().get_structcorr_type(str));
        break;

      case compute_palette_token:
        compute_.palette(str);
        break;

      case compute_saveff_token:
        compute_.saveff(TokenMapper::instance().get_boolean(str));
        break;

      case compute_savesf_token:
        compute_.savesf(TokenMapper::instance().get_boolean(str));
        break;

      case fit_param_variable_token:
        curr_fit_param_.variable_ = str;
        break;

      case fit_reference_data_path_token:
        reference_data_[0].path(str);    // TODO ...
        break;

      case fit_reference_data_mask_token:
        reference_data_[0].mask(str);    // TODO ...
        break;

      case fit_algorithm_name_token:
        curr_fit_algo_.name(TokenMapper::instance().get_fit_algorithm_name(str));
        curr_fit_algo_.name_str(str);
        break;

      case fit_algorithm_distance_metric_token:
        curr_fit_algo_.distance_metric(TokenMapper::instance().get_fit_distance_metric(str));
        break;

      case fit_algorithm_restart_token:
        curr_fit_algo_.restart(TokenMapper::instance().get_boolean(str));
        break;

      default:
        std::cerr << "fatal error: found a string '"
              << str << "' where it should not be" << std::endl;
        return false;
    } // switch

    return true;
  } // HiGInput::process_string()



  /**
   * input accessor and modifier functions
   */


  /* shapes */

  // technically some of these are not completely correct. correct them later ...
  // this function can go into the shape class ...
  bool HiGInput::compute_shape_domain(Shape &shape, vector3_t& min_dim, vector3_t& max_dim) {
    min_dim[0] = min_dim[1] = min_dim[2] = 0.0;
    max_dim[0] = max_dim[1] = max_dim[2] = 0.0;
    shape_param_iterator_t param = shape.param_begin();
    std::string shape_filename;
    switch(shape.name()) {
      case shape_box:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              std::cerr << "warning: ignoring the radius value provided for a box shape"
                    << std::endl;
              break;
            case param_xsize:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              break;
            case param_ysize:
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = -max_dim[2];
              break;
            case param_edge:
              max_dim[0] = max_dim[1] = max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = min_dim[1] = min_dim[2] = -max_dim[0];
              break;
            case param_baseangle:
              // do nothing
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_cylinder:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_xsize:
              std::cerr << "warning: ignoring the xsize values given for cylinder shape"
                    << std::endl;
              break;
            case param_ysize:
              std::cerr << "warning: ignoring the ysize values given for cylinder shape"
                    << std::endl;
              break;
            case param_height:
              max_dim[2] = 2.0 * max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              std::cerr << "warning: ignoring the edge values given for cylinder shape"
                    << std::endl;
              break;
            case param_baseangle:
              // do nothing
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_sphere:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              max_dim[2] = 2.0 * max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_xsize:
              std::cerr << "warning: ignoring the xsize values given for cylinder shape"
                    << std::endl;
              break;
            case param_ysize:
              std::cerr << "warning: ignoring the ysize values given for cylinder shape"
                    << std::endl;
              break;
            case param_height:
              max_dim[0] = max((*param).second.max(), (*param).second.min()); 
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = -max_dim[0];
              break;
            case param_edge:
              std::cerr << "warning: ignoring the edge values given for cylinder shape"
                    << std::endl;
              break;
            case param_baseangle:
              // do nothing
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_cube:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_height:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = -max_dim[0];
              break;
            case param_xsize:
            case param_ysize:
            case param_radius:
            case param_edge:
            case param_baseangle:
              // do nothing
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
            return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_pyramid:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              std::cerr << "warning: ignoring the radius values given for truncpyr shape"
                    << std::endl;
              break;
            case param_xsize:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              break;
            case param_ysize:
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_baseangle:
              // defines the angle at the base -> xy size at height z
              // do this later ...
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_trunccone:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_xsize:
              std::cerr << "warning: ignoring the xsize values given for trunccone shape"
                    << std::endl;
              break;
            case param_ysize:
              std::cerr << "warning: ignoring the ysize values given for trunccone shape"
                    << std::endl;
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              std::cerr << "warning: ignoring the edge values given for trunccone shape"
                    << std::endl;
              break;
            case param_baseangle:
              // defines the angle at the base -> xy radius at height z
              // if == 90 is cylinder, if > 90, then max x and y will change ...
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_prism3:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              std::cerr << "warning: ignoring the radius values given for prism3 shape"
                    << std::endl;
              break;
            case param_xsize:
              std::cerr << "warning: ignoring the xsize values given for prism3 shape"
                    << std::endl;
              break;
            case param_ysize:
              std::cerr << "warning: ignoring the ysize values given for prism3 shape"
                    << std::endl;
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              max_dim[0] = max_dim[1] = max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = min_dim[1] = min_dim[2] = -max_dim[0];
              break;
            case param_baseangle:
              std::cerr << "warning: ignoring the baseangle values given for prism3 shape"
                    << std::endl;
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_prism6:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              std::cerr << "warning: ignoring the radius values given for prism6 shape"
                    << std::endl;
              break;
            case param_xsize:
              std::cerr << "warning: ignoring the xsize values given for prism6 shape"
                    << std::endl;
              break;
            case param_ysize:
              std::cerr << "warning: ignoring the ysize values given for prism6 shape"
                    << std::endl;
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              max_dim[0] = max_dim[1] = max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = min_dim[1] = min_dim[2] = -max_dim[0];
              break;
            case param_baseangle:
              std::cerr << "warning: ignoring the baseangle values given for prism3 shape"
                    << std::endl;
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_prism3x:
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
              std::cerr << "warning: ignoring the radius values given for prism3x shape"
                    << std::endl;
              break;
            case param_xsize:
              max_dim[0] = max((*param).second.max(), (*param).second.min());
              min_dim[0] = -max_dim[0];
              break;
            case param_ysize:
              max_dim[1] = max((*param).second.max(), (*param).second.min());
              min_dim[1] = -max_dim[1];
              break;
            case param_height:
              max_dim[2] = max((*param).second.max(), (*param).second.min());
              min_dim[2] = 0.0;
              break;
            case param_edge:
              std::cerr << "warning: ignoring the edge values given for prism3x shape"
                    << std::endl;
              break;
            case param_baseangle:
              std::cerr << "warning: ignoring the baseangle values given for prism3x shape"
                    << std::endl;
              break;
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_sawtooth:
        std::cerr << "uh-oh: this shape has not been implemented yet" << std::endl;
        return false;
        while(param != shape.param_end()) {
          switch((*param).second.type()) {
            case param_radius:
            case param_xsize:
            case param_ysize:
            case param_height:
            case param_edge:
            case param_baseangle:
            default:
              std::cerr << "error: invalid parameter found in a shape" << std::endl;
              return false;
          } // switch
          ++ param;
        } // while
        return true;

      case shape_custom:
        shape_filename = shape.filename();
        read_shape_definition(shape_filename.c_str());
        compute_shapedef_minmax(min_dim, max_dim);
        break;

      case shape_null:
        std::cerr << "error: null shape encountered" << std::endl;
        return false;

      case shape_error:
        std::cerr << "error: the shape is an error" << std::endl;
        return false;

      default:
        std::cerr << "error: unknown shape name stored" << std::endl;
        return false;
    } // switch

    return true;
  } // HiGInput::compute_shape_domain()


  bool HiGInput::compute_shapedef_minmax(vector3_t& min_dim, vector3_t& max_dim) {
    real_t min_a = shape_def_[4], max_a = shape_def_[4];
    real_t min_b = shape_def_[5], max_b = shape_def_[5];
    real_t min_c = shape_def_[6], max_c = shape_def_[6];

    for(int i = 0; i + 6 < shape_def_.size(); i += 7) {
      min_a = (min_a > shape_def_[i + 4]) ? shape_def_[i + 4] : min_a ;
      max_a = (max_a < shape_def_[i + 4]) ? shape_def_[i + 4] : max_a ;
      min_b = (min_b > shape_def_[i + 5]) ? shape_def_[i + 5] : min_b ;
      max_b = (max_b < shape_def_[i + 5]) ? shape_def_[i + 5] : max_b ;
      min_c = (min_c > shape_def_[i + 6]) ? shape_def_[i + 6] : min_c ;
      max_c = (max_c < shape_def_[i + 6]) ? shape_def_[i + 6] : max_c ;
    } // for

    //std::cout << "------ min = " << min_a << ", " << min_b << ", " << min_c << std::endl;
    //std::cout << "------ max = " << max_a << ", " << max_b << ", " << max_c << std::endl;

    real_t diff_a = max_a - min_a;
        real_t diff_b = max_b - min_b;
        real_t diff_c = max_c - min_c;

    //std::cout << "++ diff_a = " << diff_a << ", diff_b = " << diff_b
    //      << ", diff_c = " << diff_c << std::endl;

    vector3_t axes;
        // axes[i] = j
        // i: x=0 y=1 z=2
        // j: 0=a 1=b 2=c

#ifndef AXIS_ROT    // no rotation of shape axes
        axes[0] = 0; axes[1] = 1; axes[2] = 2;
        min_dim[0] = min_a; min_dim[1] = min_b; min_dim[2] = min_c;
        max_dim[0] = max_a; max_dim[1] = max_b; max_dim[2] = max_c;
#else
        // the smallest one is x, other two are y and z
        if(diff_a < diff_b) {
            if(diff_a < diff_c) {
                // x is a
                axes[0] = 0; axes[1] = 1; axes[2] = 2;
                min_dim[0] = min_a; min_dim[1] = min_b; min_dim[2] = min_c;
                max_dim[0] = max_a; max_dim[1] = max_b; max_dim[2] = max_c;
            } else {
                // x is c
                axes[0] = 2; axes[1] = 0; axes[2] = 1;
                min_dim[0] = min_c; min_dim[1] = min_a; min_dim[2] = min_b;
                max_dim[0] = max_c; max_dim[1] = max_a; max_dim[2] = max_b;
            } // if-else
        } else {
            if(diff_b < diff_c) {
                // x is b
                axes[0] = 1; axes[1] = 0; axes[2] = 2;
                min_dim[0] = min_b; min_dim[1] = min_a; min_dim[2] = min_c;
                max_dim[0] = max_b; max_dim[1] = max_a; max_dim[2] = max_c;
            } else {
                // x is c
                axes[0] = 2; axes[1] = 0; axes[2] = 1;
                min_dim[0] = min_c; min_dim[1] = min_a; min_dim[2] = min_b;
                max_dim[0] = max_c; max_dim[1] = max_a; max_dim[2] = max_b;
            } // if-else
        } // if-else
#endif
    //if(mpi_rank == 0) {
    //  std::cout << "**              Shape size range: ("
    //        << min_dim[0] << ", " << min_dim[1] << ", " << min_dim[2] << ") x ("
    //        << max_dim[0] << ", " << max_dim[1] << ", "  << max_dim[2] << ")" << std::endl;
          /*std::cout << "++ Final shape min point: " << min_dim[0] << ", "
                      << min_dim[1] << ", " << min_dim[2] << std::endl;
          std::cout << "++ Final shape max point: " << max_dim[0] << ", "
                      << max_dim[1] << ", " << max_dim[2] << std::endl;
          std::cout << "++ Final shape dimensions: "
                      << fabs(max_dim[0] - min_dim[0]) << " x "
                      << fabs(max_dim[1] - min_dim[1]) << " x "
                      << fabs(max_dim[2] - min_dim[2]) << std::endl;*/
    //} // if

    return true;
  } // HiGInput::compute_shapedef_minmax()


  unsigned int HiGInput::read_shape_definition(const char* shape_file) {
    ShapeFileType file_type = shape_filetype(shape_file);

    if(file_type == shape_file_data) {
      return read_shape_file_data(shape_file);
    } else if(file_type == shape_file_hdf5) {
      #ifdef USE_HDF5
        return read_shape_file_hdf5(shape_file);
      #else
        std::cerr << "error: use of parallel hdf5 format has not been enabled in your installation. "
                  << "Please reinstal with the support enabled." << std::endl;
        return false;
      #endif
    } else if(file_type == shape_file_object) {
      return read_shape_file_object(shape_file);
    } else {
      std::cerr << "error: unknown shape file extension in '" << shape_file << "'" << std::endl;
      return false;
    } // if-else
    
    return true;
  } // HiGInput::read_shape_definition()


  ShapeFileType HiGInput::shape_filetype(const char* filename) {

    //return shape_file_hdf5;
    //return shape_file_data;

    std::istringstream file(filename);
    std::string s;
    while(std::getline(file, s, '.'));// std::cout << s << std::endl;
    if(s.compare("") == 0) return shape_file_null;
    if(s.compare("dat") == 0) return shape_file_data;
    if(s.compare("Dat") == 0) return shape_file_data;
    if(s.compare("DAT") == 0) return shape_file_data;
    if(s.compare("hd5") == 0) return shape_file_hdf5;
    if(s.compare("Hd5") == 0) return shape_file_hdf5;
    if(s.compare("HD5") == 0) return shape_file_hdf5;
    if(s.compare("hdf5") == 0) return shape_file_hdf5;
    if(s.compare("Hdf5") == 0) return shape_file_hdf5;
    if(s.compare("HDF5") == 0) return shape_file_hdf5;
    if(s.compare("obj") == 0) return shape_file_object;
    if(s.compare("Obj") == 0) return shape_file_object;
    if(s.compare("OBJ") == 0) return shape_file_object;
    return shape_file_error;
  } // HiGInput::shape_filetype()


  unsigned int HiGInput::read_shape_file_object(const char* filename) {
    //std::cerr << "uh-oh: given shape file type reader not yet implemented" << std::endl;
    //return false;
    unsigned int num_triangles = 0;
    double* temp_shape_def;

    // ASSUMING THERE ARE 7 ENTRIES FOR EACH TRIANGLE ... IMPROVE/GENERALIZE ...
    HiGFileReader::instance().object_shape_reader(filename, temp_shape_def, num_triangles);
    shape_def_.clear();
#ifndef KERNEL2
    shape_def_.reserve(7 * num_triangles);
    unsigned int max_size = shape_def_.max_size();
    if(7 * num_triangles > max_size) {
      std::cerr << "error: number of triangles more than what can be handled currently ["
            << max_size / 7 << "]" << std::endl;
      return 0;
    } // if
    for(unsigned int i = 0; i < 7 * num_triangles; ++ i) {
      shape_def_.push_back((real_t)temp_shape_def[i]);
    } // for
#else  // KERNEL2
    shape_def_.reserve(T_PROP_SIZE_ * num_triangles);
    unsigned int max_size = shape_def_.max_size();
    if(T_PROP_SIZE_ * num_triangles > max_size) {
      std::cerr << "error: number of triangles more than what can be handled currently ["
            << max_size / T_PROP_SIZE_ << "]" << std::endl;
      return 0;
    } // if
    for(unsigned int i = 0, count = 0; i < T_PROP_SIZE_ * num_triangles; ++ i) {
      if((i + 1) % T_PROP_SIZE_ == 0) shape_def_.push_back((real_t)0.0); // for padding
      else shape_def_.push_back((real_t)temp_shape_def[count ++]);
    } // for
#endif // KERNEL2
    return num_triangles;
  } // HiGInput::read_shape_file_object()


  #ifdef USE_HDF5
  unsigned int HiGInput::read_shape_file_hdf5(const char* filename) {
    unsigned int num_triangles = 0;
    double* temp_shape_def;

    // ASSUMING THERE ARE 7 ENTRIES FOR EACH TRIANGLE ... IMPROVE/GENERALIZE ...
    HiGFileReader::instance().hdf5_shape_reader(filename, temp_shape_def, num_triangles);
    shape_def_.clear();
    #ifndef KERNEL2
    shape_def_.reserve(7 * num_triangles);
    unsigned int max_size = shape_def_.max_size();
    if(7 * num_triangles > max_size) {
      std::cerr << "error: number of triangles more than what can be handled currently ["
            << max_size / 7 << "]" << std::endl;
      return 0;
    } // if
    for(unsigned int i = 0; i < 7 * num_triangles; ++ i) {
      shape_def_.push_back((real_t)temp_shape_def[i]);
    } // for
    #else  // KERNEL2
    shape_def_.reserve(T_PROP_SIZE_ * num_triangles);
    unsigned int max_size = shape_def_.max_size();
    if(T_PROP_SIZE_ * num_triangles > max_size) {
      std::cerr << "error: number of triangles more than what can be handled currently ["
            << max_size / T_PROP_SIZE_ << "]" << std::endl;
      return 0;
    } // if
    for(unsigned int i = 0, count = 0; i < T_PROP_SIZE_ * num_triangles; ++ i) {
      if((i + 1) % T_PROP_SIZE_ == 0) shape_def_.push_back((real_t)0.0); // for padding
      else shape_def_.push_back((real_t)temp_shape_def[count ++]);
    } // for
    #endif // KERNEL2
    return num_triangles;
  } // HiGInput::read_shape_file_hdf5()
  #endif // USE_HDF5


  unsigned int HiGInput::read_shape_file_data(const char* filename) {
    //std::cerr << "uh-oh: given shape file type reader not yet implemented" << std::endl;
    //return false;

    unsigned int num_triangles = 0;
    HiGFileReader::instance().shape_shape_reader(filename, shape_def_, num_triangles);

    return true;
  } // HiGInput::read_shape_file_data()


  /* grains */

  bool HiGInput::construct_lattice_vectors() {
    if(structures_.size() < 1) return false;
    for(structure_iterator_t i = structures_.begin(); i != structures_.end(); ++ i) {
      if(!(*i).second.construct_lattice_vectors()) return false;
    } // for

    return true;
  } // HiGInput::construct_lattice_vectors()


  /* layers */

  unsigned int HiGInput::num_layers() const {
    // -1 is substrate layer
    // 0 is vacuum
    // dont count these two
    //if(has_substrate_layer() && layers_.count(0) != 0) return layers_.size() - 2;
    //if(has_substrate_layer() || layers_.count(0) != 0) return layers_.size() - 1;
    //return layers_.size();
    if(has_substrate_layer() && has_vacuum_layer())
      return layers_.size() - 2;
    if((!has_substrate_layer()) && has_vacuum_layer() ||
        has_substrate_layer() && (!has_vacuum_layer())) 
      return layers_.size() - 1;
    return layers_.size();
  } // HiGInput::num_layers()


  bool HiGInput::is_single_layer() const {
    //if(has_substrate_layer() && has_vacuum_layer() && layers_.size() == 3 ||
    //    (!has_substrate_layer()) && has_vacuum_layer() && layers_.size() == 2 ||
    //    has_substrate_layer() && (!has_vacuum_layer()) && layers_.size() == 2 ||
    //    (!has_substrate_layer()) && (!has_vacuum_layer()) && layers_.size == 1)
    //  return true;
    //else
    //  return false;
    return (num_layers() == 1);
  } // HiGInput::is_single_layer()


  bool HiGInput::has_vacuum_layer() const {
    return (layers_.count(0) != 0);
  } // HiGInput::has_vacuum_layer()


  bool HiGInput::has_substrate_layer() const {  // substrate is the one with order -1
                          // it extends to infinity
    //layer_iterator_t i = layers_.begin();
    /*auto i = layers_.begin();
    while(i != layers_.end() && (*i).second.order() != -1) i ++;
    if(i == layers_.end()) return false;
    return true;*/
    return (layers_.count(-1) != 0);
  } // HiGInput::substrate_layer()


  Layer& HiGInput::substrate_layer() {  // substrate is the one with order -1
                      // it extends to infinity
    //layer_iterator_t i = layers_.begin();
    /*auto i = layers_.begin();
    while(i != layers_.end() && (*i).second.order() != -1) ++ i;
    if(i == layers_.end()) return (*i).second;  // what to send here ... ? NULL doesnt work ...
    return (*i).second;*/
    return layers_[-1];
  } // HiGInput::substrate_layer()


  RefractiveIndex HiGInput::substrate_refindex() {
    if(has_substrate_layer())
      return layers_[-1].refindex();
    else
      return RefractiveIndex(0, 0);
  } // HiGInput::substrate_refindex()


  Layer& HiGInput::single_layer() {  // if there is exactly 1 layer
                    // excluding substrate and vacuum
    layer_iterator_t i = layers_.begin();
    if(has_substrate_layer() && has_vacuum_layer() && layers_.size() == 3 ||
        (!has_vacuum_layer()) && has_substrate_layer() && layers_.size() == 2 ||
        (!has_substrate_layer()) && has_vacuum_layer() && layers_.size() == 2 ||
        (!has_substrate_layer()) && (!has_vacuum_layer()) && layers_.size() == 1) {
      while(i != layers_.end() && (*i).first != 0) ++ i;
      if((*i).first == 0) ++i;
      if(i != layers_.end()) return (*i).second;
      else return layers_[0];
    //} else if(has_substrate_layer() && layers_.size() == 2) {
    //  if((*i).first == -1) { ++ i; return (*i).second; }
    //  else return (*i).second;
    } else {
      std::cerr << "error: single_layer requested on multiple layers" << std::endl;
      return (*i).second;
    } // if-else
  } // HiGInput::single_layer()


  int HiGInput::min_layer_order() {
    layer_iterator_t iter = layers_.begin();
    while((*iter).second.order() < 0) ++ iter;      // since layers are sorted
    return (*iter).second.order();
  } // HiGInput::min_layer_order()


/*  struct CompareLayers {
      bool operator() (const std::pair<std::string, Layer>& a, const std::pair<std::string, Layer>& b) { return (a.order() < b.order()); }
  } comp_layer;
*/

  bool HiGInput::construct_layer_profile() {
    // if there is substrate, its the first one, followed by others
    // insert layer 0, with refindex 0, 0
    
    if(has_vacuum_layer()) return true;
    Layer vacuum;
    vacuum.key(std::string("vacuum"));
    vacuum.refindex_delta(0.0);
    vacuum.refindex_beta(0.0);
    vacuum.thickness(0.0);
    vacuum.order(0);
    vacuum.z_val(0.0);
    layers_[vacuum.order()] = vacuum;
    // compute the cumulative z value
    real_t curr_z = 0.0;
    for(layer_iterator_t i = layers_.begin(); i != layers_.end(); i ++) {
      if((*i).second.order() == -1) { (*i).second.z_val(0.0); continue; }
      if((*i).second.order() == 0) continue;
      curr_z = curr_z - (*i).second.thickness();
      (*i).second.z_val(curr_z);
    } // for

    return true;
  } // HiGInput::construct_layer_profile()


  /* structures */

  unsigned int HiGInput::num_structures() const {
    return structures_.size();
  } // HiGInput::num_structures()


  real_t HiGInput::layers_z_min() {    // check ... (*i).first is layer order, not position ...
    real_t min_val = FLT_MAX;
    for(layer_iterator_t i = layers_.begin(); i != layers_.end(); i ++) {
      if(min_val > (*i).first && (*i).first >= 0) min_val = (*i).first;
    } // for
    return min_val;
  } // HiGInput::layers_z_min()


  bool HiGInput::compute_domain_size(vector3_t& min_vec, vector3_t& max_vec,
                    real_t& z_min_0, real_t& z_max_0) {
    real_t ma = FLT_MAX;
    real_t mi = -FLT_MAX;

    vector3_t max_l(mi, mi, layers_z_min());
    vector3_t min_l(ma, ma, ma);
    z_max_0 = mi;
    z_min_0 = ma;

    // iterate over structures
    for(structure_iterator_t s = structures_.begin(); s != structures_.end(); s ++) {

//#ifdef __INTEL_COMPILER
//      Unitcell *curr_unitcell = NULL;
//      if(unitcells_.count((*s).second.grain_unitcell_key()) > 0)
//        curr_unitcell = &unitcells_[(*s).second.grain_unitcell_key()];
//      else return false;
//#else
      Unitcell *curr_unitcell = &unitcells_.at((*s).second.grain_unitcell_key());
//#endif

      vector3_t element_min(REAL_ZERO_, REAL_ZERO_, REAL_ZERO_);
      vector3_t element_max(REAL_ZERO_, REAL_ZERO_, REAL_ZERO_);
      for(Unitcell::element_iterator_t e = curr_unitcell->element_begin();
          e != curr_unitcell->element_end(); ++ e) {

        Shape curr_shape = shapes_[(*e).first];
        vector3_t shape_min(0.0, 0.0, 0.0), shape_max(0.0, 0.0, 0.0);
        compute_shape_domain(curr_shape, shape_min, shape_max);

        /*std::cout << "++ Shape min point: " << shape_min[0] << ", " << shape_min[1]
              << ", " << shape_min[2] << std::endl;
        std::cout << "++ Shape max point: " << shape_max[0] << ", " << shape_max[1]
              << ", " << shape_max[2] << std::endl;
        std::cout << "++ Shape dimensions: " << shape_max[0] - shape_min[0] << " x "
              << shape_max[1] - shape_min[1] << " x "
              << shape_max[2] - shape_min[2] << std::endl;*/

        element_min[0] = std::min(element_min[0], shape_min[0]);
        element_min[1] = std::min(element_min[1], shape_min[1]);
        element_min[2] = std::min(element_min[2], shape_min[2]);
        element_max[0] = std::max(element_max[0], shape_max[0]);
        element_max[1] = std::max(element_max[1], shape_max[1]);
        element_max[2] = std::max(element_max[2], shape_max[2]);
      } // for elements

      /* determine the structure's position in the sample configuration */
      real_t zc_l = layer_origin_z((*s).second);

      vector3_t n = (*s).second.grain_repetition();
      -- n[0]; -- n[1]; -- n[2];

      // get the lattice vectors a, b, c t, and translate
      Lattice *curr_lattice = (Lattice*)(*s).second.lattice();
      vector3_t a = curr_lattice->a();
      vector3_t b = curr_lattice->b();
      vector3_t c = curr_lattice->c();
      vector3_t t = curr_lattice->t();
      vector3_t transvec = (*s).second.grain_transvec();
      t[0] += transvec[0]; t[1] += transvec[1]; t[2] += transvec[2];

      vector3_t a_max(0.0, 0.0, 0.0), b_max(0.0, 0.0, 0.0), c_max(0.0, 0.0, 0.0);
      vector3_t a_min(0.0, 0.0, 0.0), b_min(0.0, 0.0, 0.0), c_min(0.0, 0.0, 0.0);

      // a along x
      if(a[0] > 0) {
        a_max[0] = n[0] * a[0] + transvec[0] + element_max[0];
        a_min[0] = transvec[0] + element_min[0];
      } else {
        a_max[0] = transvec[0] + element_max[0];
        a_min[0] = n[0] * a[0] + transvec[0] + element_min[0];
      } // if-else
      // a along y
      if(a[1] > 0) {
        a_max[1] = n[0] * a[1] + transvec[1] + element_max[1];
        a_min[1] = transvec[1] + element_min[1];
      } else {
        a_max[1] = transvec[1] + element_max[1];
        a_min[1] = n[0] * a[1] + transvec[1] + element_min[1];
      } // if-else
      // a along z
      if(a[2] > 0) {
        a_max[2] = n[0] * a[2] + zc_l + element_max[2];
        a_min[2] = zc_l + element_min[2];
      } else {
        a_max[2] = zc_l + element_max[2];
        a_min[2] = n[0] * a[2] + zc_l + element_min[2];
      } // if-else
      
      // b along x
      if(b[0] > 0) {
        b_max[0] = n[1] * b[0] + transvec[0] + element_max[0];
        b_min[0] = transvec[0] + element_min[0];
      } else {
        b_max[0] = transvec[0] + element_max[0];
        b_min[0] = n[1] * b[0] + transvec[0] + element_min[0];
      } // if-else
      // b along y
      if(b[1] > 0) {
        b_max[1] = n[1] * b[1] + transvec[1] + element_max[1];
        b_min[1] = transvec[1] + element_min[1];
      } else {
        b_max[1] = transvec[1] + element_max[1];
        b_min[1] = n[1] * b[1] + transvec[1] + element_min[1];
      } // if-else
      // b along z
      if(b[2] > 0) {
        b_max[2] = n[1] * b[2] + zc_l + element_max[2];
        b_min[2] = zc_l + element_min[2];
      } else {
        b_max[2] = zc_l + element_max[2];
        b_min[2] = n[1] * b[2] + zc_l + element_min[2];
      } // if-else
      
      // c along x
      if(c[0] > 0) {
        c_max[0] = n[2] * c[0] + transvec[0] + element_max[0];
        c_min[0] = transvec[0] + element_min[0];
      } else {
        c_max[0] = transvec[0] + element_max[0];
        c_min[0] = n[2] * c[0] + transvec[0] + element_min[0];
      } // if-else
      // c along y
      if(c[1] > 0) {
        c_max[1] = n[2] * c[1] + transvec[1] + element_max[1];
        c_min[1] = transvec[1] + element_min[1];
      } else {
        c_max[1] = transvec[1] + element_max[1];
        c_min[1] = n[2] * c[1] + transvec[1] + element_min[1];
      } // if-else
      // c along z
      if(c[2] > 0) {
        c_max[2] = n[2] * c[2] + zc_l + element_max[2];
        c_min[2] = zc_l + element_min[2];
      } else {
        c_max[2] = zc_l + element_max[2];
        c_min[2] = n[2] * c[2] + zc_l + element_min[2];
      } // if-else

      vector3_t d_min, d_max;
      d_min[0] = t[0] + min(a_min[0], b_min[0], c_min[0]);
      d_max[0] = t[0] + max(a_max[0], b_max[0], c_max[0]);
      d_min[1] = t[1] + min(a_min[1], b_min[1], c_min[1]);
      d_max[1] = t[1] + max(a_max[1], b_max[1], c_max[1]);
      d_min[2] = t[2] + min(a_min[2], b_min[2], c_min[2]);
      d_max[2] = t[2] + max(a_max[2], b_max[2], c_max[2]);

      // case with structure elements in vacuum
      Layer curr_layer = layers_[layer_key_map_[(*s).second.grain_layer_key()]];
      if(curr_layer.order() == 0) {
        z_min_0 = (d_min[2] < z_min_0) ? d_min[2] : z_min_0;
        z_max_0 = (d_max[2] > max_l[2]) ? d_max[2] : z_max_0;
      } // if

      // compute values of min_l and max_l
      max_l[0] = max(max_l[0], d_max[0]);
      max_l[1] = max(max_l[1], d_max[1]);
      max_l[2] = max(max_l[2], d_max[2]);
      min_l[0] = min(min_l[0], d_min[0]);
      min_l[1] = min(min_l[1], d_min[1]);
      min_l[2] = min(min_l[2], d_min[2]);
    } // for structures

    max_vec[0] = max_l[0];
    max_vec[1] = max_l[1];
    max_vec[2] = max_l[2];
    min_vec[0] = min_l[0];
    min_vec[1] = min_l[1];
    min_vec[2] = min_l[2];

    z_min_0 = min(min_l[2], z_min_0);
    z_max_0 = max(max_l[2], z_max_0);

    return true;
  } // HiGInput::compute_domain_size()


  /**
   * fitting related functions
   */

  bool HiGInput::update_params(const map_t& params) {
    //print_all();
    for(map_t::const_iterator p = params.begin(); p != params.end(); ++ p) {
      real_t new_val = (*p).second;
      ParamSpace ps = param_space_key_map_.at((*p).first);  // if not exist, exception!!
      // check if new_val is within the param space
      //if(new_val < ps.min_ || new_val > ps.max_) {
      //  std::cerr << "warning: given parameter value out of range space. resetting to limit."
      //        << std::endl;
      //  new_val = std::max(std::min(new_val, ps.max_), ps.min_);
      //} // if
      std::string param = param_key_map_.at((*p).first);  // if not exist, exception!!
      // get first component from the string
      std::string keyword, rem_param;
      if(!extract_first_keyword(param, keyword, rem_param)) return false;
      // get keyword name and key (if any)
      std::string keyword_name, keyword_key;
      if(!extract_keyword_name_and_key(keyword, keyword_name, keyword_key)) return false;
      std::string rem_param2;
      switch(TokenMapper::instance().get_keyword_token(keyword_name)) {
        case shape_token:
          #ifdef __INTEL_COMPILER
            if(shapes_.count(keyword_key) == 0 ||
                !shapes_[keyword_key].update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #else
            if(!shapes_.at(keyword_key).update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #endif // __INTEL_COMPILER
          break;

        case layer_token:
          #ifdef __INTEL_COMPILER
            if(layer_key_map_.count(keyword_key) == 0 ||
                layers_.count(layer_key_map_[keyword_key]) == 0 ||
                !(layers_[layer_key_map_[keyword_key]]).update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #else
            if(!layers_.at(layer_key_map_.at(keyword_key)).update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #endif
          break;

        case struct_token:
          #ifdef __INTEL_COMPILER
            if(structures_.count(keyword_key) == 0 ||
                !structures_[keyword_key].update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #else
            if(!structures_.at(keyword_key).update_param(rem_param, new_val)) {
              std::cerr << "error: failed to update param '" << param << "'" << std::endl;
              return false;
            } // if
          #endif
          break;

        case instrument_token:
          extract_first_keyword(rem_param, keyword, rem_param2);
          switch(TokenMapper::instance().get_keyword_token(keyword)) {
            case instrument_scatter_token:
              if(!scattering_.update_param(rem_param2, new_val)) {
                std::cerr << "error: failed to update param '" << param << "'"
                      << std::endl;
                return false;
              } // if
              break;

            case instrument_detector_token:
              if(!detector_.update_param(rem_param2, new_val)) {
                std::cerr << "error: failed to update param '" << param << "'"
                      << std::endl;
                return false;
              } // if
              break;

            case error_token:
              std::cerr << "error: invalid keyword '" << keyword
                    << "' in parameter variable name '" << param << "'"
                    << std::endl;
              return false;

            default:
              std::cerr << "error: misplaced keyword '" << keyword
                    << "' in parameter variable name '" << param << "'"
                    << std::endl;
              return false;
          } // switch
          break;

        case compute_token:
          if(!compute_.update_param(rem_param, new_val)) {
            std::cerr << "error: failed to update param '" << param << "'" << std::endl;
            return false;
          } // if
          break;

        case error_token:
          std::cerr << "error: invalid keyword '" << keyword_name
                << "' in parameter variable name '" << param << "'"
                << std::endl;
          return false;

        default:
          std::cerr << "error: misplaced keyword '" << keyword_name
                << "' in parameter variable name '" << param << "'"
                << std::endl;
          return false;
      } // switch
    } // for
    return true;
  } // HiGInput::update_params()


  /** print functions for testing only
   */


  void HiGInput::print_all() {
    std::cout << "HipGISAXS Inputs: " << std::endl;
    print_shapes();
    print_unitcells();
    print_layers();
    print_structures();
    print_scattering_params();
    print_detector_params();
    print_compute_params();
    print_fit_params();
    print_ref_data();
    print_fit_algos();
  } // HiGInput::print_all()


  void HiGInput::print_shapes() {
    std::cout << "Shapes:" << std::endl;
    for(shape_iterator_t i = shapes_.begin(); i != shapes_.end(); i ++) {
      (*i).second.print();
    } // for
  } // HiGInput::print_shapes()


  void HiGInput::print_unitcells() {
    std::cout << "Unitcells:" << std::endl;
    for(unitcell_iterator_t u = unitcells_.begin(); u != unitcells_.end(); ++ u) {
      (*u).second.print();
    } // for
  } // HiGInput::print_unitcells()


  void HiGInput::print_layers() {
    std::cout << "Layers:" << std::endl;
    for(layer_iterator_t i = layers_.begin(); i != layers_.end(); i ++) {
      (*i).second.print();
    } // for
  } // HiGInput::print_layers()


  void HiGInput::print_structures() {
    std::cout << "Structures:" << std::endl;
    for(structure_iterator_t i = structures_.begin(); i != structures_.end(); i ++) {
      (*i).second.print();
    } // for
  } // HiGInput::print_structures()


  void HiGInput::print_scattering_params() {
    scattering_.print();
  } // HiGInput::print_scattering_params()


  void HiGInput::print_detector_params() {
    detector_.print();
  } // HiGInput::print_detector_params()


  void HiGInput::print_compute_params() {
    compute_.print();
  } // HiGInput::print_compute_params()

  void HiGInput::print_fit_params() {
    if(param_key_map_.empty()) return;
    std::cout << "Fit Parameters: " << std::endl;
    for(std::map <std::string, std::string>::const_iterator i = param_key_map_.begin();
        i != param_key_map_.end(); ++ i) {
      ParamSpace temp = param_space_key_map_.at((*i).first);
      FitParam temp2 = param_data_key_map_.at((*i).first);
      std::cout << "  " << (*i).first << ": [" << temp.min_ << " " << temp.max_ << "] "
        << temp2.key_ << " " << temp2.variable_ << " " << temp2.init_
        << " (" << (*i).second << ")" << std::endl;
    } // for
  } // HiGInput::print_fit_params()

  void HiGInput::print_ref_data() {
    if(!reference_data_set_) return;
    reference_data_[0].print();
  } // HiGInput::print_ref_data()

  void HiGInput::print_fit_algos() {
    if(analysis_algos_.empty()) return;
    std::cout << "Analysis Algorithms: " << std::endl;
    for(analysis_algo_list_t::const_iterator i = analysis_algos_.begin();
        i != analysis_algos_.end(); ++ i) {
      (*i).print();
    } // for
  } // HiGInput::print_fit_algos()

} // namespace hig
