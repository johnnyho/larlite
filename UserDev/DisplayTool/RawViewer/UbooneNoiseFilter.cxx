#ifndef UBOONENOISEFILTER_CXX
#define UBOONENOISEFILTER_CXX

#include "UbooneNoiseFilter.h"

namespace larlite {

unsigned int detPropFetcher::n_wires(unsigned int plane) {
  if (plane == 0)
    return 2400;
  if (plane == 1)
    return 2400;
  if (plane == 2)
    return 3456;
}
unsigned int detPropFetcher::n_planes() {
  return 3;
}

}

namespace ub_noise_filter {

UbooneNoiseFilter::UbooneNoiseFilter() {

  _pedestal_by_plane.resize(_detector_properties_interface.n_planes());
  _rms_by_plane.resize(_detector_properties_interface.n_planes());
  _wire_status_by_plane.resize(_detector_properties_interface.n_planes());
  _chirp_info_by_plane.resize(_detector_properties_interface.n_planes());

  for (int plane = 0; plane < _detector_properties_interface.n_planes(); plane ++) {
    _pedestal_by_plane.at(plane).resize(_detector_properties_interface.n_wires(plane));
    _rms_by_plane.at(plane).resize(_detector_properties_interface.n_wires(plane));
    _wire_status_by_plane.at(plane).resize(_detector_properties_interface.n_wires(plane));
  }
}

void UbooneNoiseFilter::set_data(std::vector<std::vector<float> > * _input_data) {

  if (!_input_data) {
    throw std::runtime_error("NoiseFilterException: Bad pointer to data set provided.");
  }
  else {
    _data_by_plane = _input_data;
  }

}

void UbooneNoiseFilter::reset_internal_data() {
  // Clear the internal data:

  // Clear the pedestal data without needing a reallocation:
  for (auto & _ped_plane : _pedestal_by_plane) {
    for (auto & _wire_ped : _ped_plane) {
      _wire_ped = 0;
    }
  }

  // Similarly, clear the rms data:
  for (auto & _rms_plane : _rms_by_plane) {
    for (auto & _wire_rms : _rms_plane) {
      _wire_rms = 0;
    }
  }

  // Similarly, clear the rms data:
  for (auto & _wire_status_plane : _wire_status_by_plane) {
    for (auto & _status : _wire_status_plane) {
      _status = kNormal;
    }
  }


  // Clear the chirping data, but since it's a map
  // and it's sparse delete the past data.
  for (auto & _chirp_map : _chirp_info_by_plane) {
    _chirp_map.clear();
  }

  correlatedNoiseWaveforms.clear();
  correlatedNoiseWaveforms.resize(correlated_noise_blocks.size());
  for (int i = 0; i < correlated_noise_blocks.size(); i ++) {
    correlatedNoiseWaveforms.at(i).resize(correlated_noise_blocks.at(i).size() - 1);
  }


}

void UbooneNoiseFilter::set_n_time_ticks(unsigned int _n_time_ticks) {
  this->_n_time_ticks_data = _n_time_ticks;
}


void UbooneNoiseFilter::clean_data() {

  // Make sure everything is cleaned up:
  reset_internal_data();

  tag_special_wire_statuses();

  // This function cleans the data up.
  //
  // First, do pedestal subtraction and determine if the wire is chirping:

  for (unsigned int plane = 0; plane < _detector_properties_interface.n_planes(); plane ++) {

    // Loop over the wires within the plane
    for (unsigned int wire = 0; wire < _detector_properties_interface.n_wires(plane); wire ++) {


      size_t offset = wire * _n_time_ticks_data;

      // Very first step:  determine if this wire is chirping.

      // The wire in question is from _data_by_plane[plane][offset]
      // to _data_by_plane[plane][offset + _n_time_ticks_data]
      float * _wire_arr = &(_data_by_plane->at(plane).at(offset));
      bool wire_is_chirping = is_chirping(_wire_arr,
                                          _n_time_ticks_data,
                                          wire,
                                          plane);

      // if (wire_is_chirping && plane == 1 ) {
      //   if (_chirp_info_by_plane[plane][wire].chirp_frac != 1.0) {
      //     std::cout << "Plane " << plane << ", wire " << wire
      //               << " is chirping.\n"
      //               << "   Chirping start: "
      //               << _chirp_info_by_plane[plane][wire].chirp_start << "\n"
      //               << "   Chirping stop: "
      //               << _chirp_info_by_plane[plane][wire].chirp_stop
      //               << "   Chirping frac: "
      //               << _chirp_info_by_plane[plane][wire].chirp_frac
      //               << std::endl;
      //   }
      // }

      // Do the full pedestal subtraction:
      get_pedestal_info(_wire_arr, _n_time_ticks_data, wire, plane);

      apply_moving_average(_wire_arr, _n_time_ticks_data, wire, plane);

      // rescale_by_rms(_wire_arr, _n_time_ticks_data, wire, plane);

    }


    // To remove the correlated noise, loop over blocks of wires:
    for (unsigned int plane = 0; plane < _detector_properties_interface.n_planes(); plane ++) {
      for (size_t i_block = 0; i_block < correlated_noise_blocks.at(plane).size() - 1; i_block ++) {
        int block_wire_start = correlated_noise_blocks.at(plane).at(i_block);

        // std::cout << "Plane " << plane << ", i_block " << i_block << std::endl;

        size_t offset = block_wire_start * _n_time_ticks_data;


        float * _wire_block = &(_data_by_plane->at(plane).at(offset));


        build_correlated_noise_waveforms(_wire_block,
                                         i_block,
                                         plane,
                                         _n_time_ticks_data);
      }
    }
  }

  for (unsigned int plane = 0; plane < _detector_properties_interface.n_planes(); plane ++) {

    // Loop over the wires within the plane
    for (unsigned int wire = 0; wire < _detector_properties_interface.n_wires(plane); wire ++) {


      size_t offset = wire * _n_time_ticks_data;

      float * _wire_arr = &(_data_by_plane->at(plane).at(offset));

      remove_correlated_noise(_wire_arr, _n_time_ticks_data, wire, plane);
    }
  }

  // Print out a few values just for debugging:
  int plane = 2;
  for (int wire = 287; wire < 310; wire ++) {
    std::cout << "Plane " << plane << ", wire " << wire << " status is ";
    if (_wire_status_by_plane[plane][wire] == kNormal) {
      std::cout << "Normal";
    }
    else if (_wire_status_by_plane[plane][wire] == kChirping) {
      std::cout << "Chirping";
    }
    else if (_wire_status_by_plane[plane][wire] == kDead) {
      std::cout << "Dead";
    }
    else if (_wire_status_by_plane[plane][wire] == kHighNoise) {
      std::cout << "HighNoise";
    }
    std::cout << ", RMS is " << _rms_by_plane[plane][wire] << std::endl;
  }
  return;
}

void UbooneNoiseFilter::get_pedestal_info(float * _data_arr, int N, int wire, int plane) {

  int min_subrange_n = 500;
  int min_submedian_n = 101; ///KEEP THIS NUMBER ODD!

  int start_tick = 0;
  int end_tick = N;


  if (_chirp_info_by_plane[plane].find(wire) != _chirp_info_by_plane[plane].end()) {
    // this wire IS chirping, so only use the good range:
    // Either start or the end of the wire will be one range of the chirping.
    if (_chirp_info_by_plane[plane][wire].chirp_start == 0) {
      start_tick = _chirp_info_by_plane[plane][wire].chirp_stop;
      end_tick = _n_time_ticks_data;
    }
    else {
      start_tick = 0.0;
      end_tick = _chirp_info_by_plane[plane][wire].chirp_start;
    }
  }
  else {
    // Then this wire is not chirping, use the whole range for pedestal subtraction

  }

  int total_ticks = end_tick - start_tick;

  int n_ranges = total_ticks / min_subrange_n;

  // If there are less than the min number of ticks, set pedestal and rms to zero
  // and declare this wire dead.

  if (total_ticks < min_subrange_n) {
    _pedestal_by_plane[plane][wire] = 0.0;
    _rms_by_plane[plane][wire] = 0.0;
    _wire_status_by_plane[plane][wire] = kDead;
    return;
  }


  // Loop over the n ranges and sample evenly within each range to
  // accumulate a list of medians

  std::vector<float> median_list;


  for (int i = 0; i < n_ranges; i ++) {
    std::vector<float> _median_accumulator;
    _median_accumulator.reserve(min_submedian_n);
    int step_size = min_subrange_n / min_submedian_n;
    int this_start_tick = start_tick + min_subrange_n * i;
    int this_end_tick = start_tick + min_subrange_n * (i + 1);
    for (int tick = this_start_tick; tick < this_end_tick; tick += step_size) {
      _median_accumulator.push_back(_data_arr[tick]);
    }
    // Now get the median:
    std::nth_element(
      _median_accumulator.begin(),
      _median_accumulator.begin() +
      _median_accumulator.size() / 2,
      _median_accumulator.end() );
    median_list.push_back(_median_accumulator.at(_median_accumulator.size() / 2) );
  }

  // Now, take the mean of the medians as the pedestal:
  _pedestal_by_plane[plane][wire] = std::accumulate(median_list.begin(), median_list.end(), 0.0) / (float) n_ranges;


  // Now, go through and do the pedestal subtraction.
  // Calculate the rms as it goes:


  if (_pedestal_by_plane[plane][wire] != _pedestal_by_plane[plane][wire]) {
    std::cout << "nan encountered in plane " << plane << " on wire " << wire << "!" << std::endl;
    exit(-1);
  }

  float rms_accumulator = 0;
  int n_rms = 0.0;
  int n_rms_max = 400;
  int step_size = total_ticks / n_rms_max;
  for (int tick = start_tick; tick < end_tick; tick ++) {
    _data_arr[tick] -= _pedestal_by_plane[plane][wire];

    // Only take a few hundred points for the rms,
    // and skip around so that they aren't all from the same spot
    if (tick % step_size == 0) {
      rms_accumulator += _data_arr[tick] * _data_arr[tick];
      n_rms ++;
    }
  }

  // std::cout << "rms_accumulator " << rms_accumulator << std::endl;
  // std::cout << "n_rms " << n_rms << std::endl;

  _rms_by_plane[plane][wire] = sqrt(rms_accumulator / n_rms);
  // std::cout << "_rms_by_plane[plane][wire] " << _rms_by_plane[plane][wire] << std::endl;

  // exit(0);

  // Here, do the cuts on being dead, high RMS, etc.

  if (_rms_by_plane[plane][wire] < _lowRMS_cutoff[plane]) {
    _wire_status_by_plane[plane][wire] = kDead;
    for (int tick = start_tick; tick < end_tick; tick ++) {
      _data_arr[tick] = 0.0;
    }
  }
  if (_rms_by_plane[plane][wire] > _highRMS_cutoff[plane]) {
    _wire_status_by_plane[plane][wire] = kHighNoise;
    // Zero out the wire:
    for (int tick = start_tick; tick < end_tick; tick ++) {
      _data_arr[tick] = 0.0;
    }
  }





}


void UbooneNoiseFilter::rescale_by_rms(float * _data_arr, int N, unsigned int wire, unsigned int plane) {

  // If the wire status is normal or chirping, rescale the wire by the rms to set the RMS to 1.0


  if (_wire_status_by_plane[plane][wire] == kNormal) {
    float scale = 1.0 / _rms_by_plane[plane][wire];
    for (int tick = 0; tick < N; tick ++) {
      _data_arr[tick] *= scale;
    }
  }
}


void UbooneNoiseFilter::apply_moving_average(
  float * _data_arr,
  int N,
  int wire,
  int plane)
{

  // // Skip if the wire is dead
  // if (_wire_status_by_plane[plane][wire] == kDead) {
  //   return;
  // }

  int start_tick = 0;
  int end_tick = N;


  if (_chirp_info_by_plane[plane].find(wire) != _chirp_info_by_plane[plane].end()) {
    // this wire IS chirping, so only use the good range:
    // Either start or the end of the wire will be one range of the chirping.
    if (_chirp_info_by_plane[plane][wire].chirp_start == 0) {
      start_tick = _chirp_info_by_plane[plane][wire].chirp_stop;
      end_tick = _n_time_ticks_data;
    }
    else {
      start_tick = 0.0;
      end_tick = _chirp_info_by_plane[plane][wire].chirp_start;
    }
  }
  else {
    // Then this wire is not chirping, use the whole range for pedestal subtraction

  }

  // Each bin is replaced by the average of it and the next bin;
  // Default is 2 bins to remove zig zag noise.

  int i;
  for (int tick = start_tick; tick < end_tick - 1 ; tick ++) {

    _data_arr[tick] = 0.5 * (_data_arr[tick] + _data_arr[tick + 1]);

  }


}

void UbooneNoiseFilter::remove_correlated_noise(
  float * _data_arr,
  int N,
  unsigned int wire,
  unsigned int plane) {

  if (_wire_status_by_plane[plane][wire] == kDead ||
      _wire_status_by_plane[plane][wire] == kHighNoise) {
    // Make sure all the ticks are zeroed out:
    for (int tick = 0; tick < N; tick ++) {
      _data_arr[tick] = 0.0;
    }
    return;
  }

  // First, need to know what block this wire came from:
  int i_block;
  for (i_block = 0;
       i_block < correlated_noise_blocks.at(plane).size();
       i_block ++)
  {
    if (correlated_noise_blocks.at(plane).at(i_block + 1) > wire) {
      // Then the block is the correct one!
      break;
    }
  }

  // Now subtract the waveform from this wire
  int start_tick = 0;
  int end_tick = _n_time_ticks_data;


  if (_chirp_info_by_plane[plane].find(wire) != _chirp_info_by_plane[plane].end()) {
    // this wire IS chirping, so only use the good range:
    // Either start or the end of the wire will be one range of the chirping.
    if (_chirp_info_by_plane[plane][wire].chirp_start == 0) {
      start_tick = _chirp_info_by_plane[plane][wire].chirp_stop;
      end_tick = _n_time_ticks_data;
    }
    else {
      start_tick = 0.0;
      end_tick = _chirp_info_by_plane[plane][wire].chirp_start;
    }
  }
  else {
    // Then this wire is not chirping, use the whole range for pedestal subtraction

  }

  for (int tick = start_tick; tick < end_tick; tick ++) {
    _data_arr[tick] -= correlatedNoiseWaveforms[plane][i_block][tick];
  }

  return;

}



void UbooneNoiseFilter::build_correlated_noise_waveforms(
  float * _wire_block,
  int i_block,
  int plane,
  int _n_time_ticks_data)
{


  // std::cout << "correlatedNoiseWaveforms.size() " << correlatedNoiseWaveforms.size() << std::endl;
  // std::cout << "correlatedNoiseWaveforms.at(plane).size() " << correlatedNoiseWaveforms.at(plane).size() << std::endl;
  correlatedNoiseWaveforms.at(plane).at(i_block).clear();
  correlatedNoiseWaveforms.at(plane).at(i_block).resize(_n_time_ticks_data);

  int block_wire_start = correlated_noise_blocks.at(plane).at(i_block);
  // std::cout << "block_wire_start " << block_wire_start << std::endl;
  int block_wire_end = correlated_noise_blocks.at(plane).at(i_block + 1);
  // std::cout << "block_wire_end " << block_wire_end << std::endl;

  // Loop over the wires in this block and build a correlated noise waveform from them.
  std::vector<float> _median_accumulator;

  // Loop over every tick in this block, which takes some time ...
  int offset;
  for (int tick = 0; tick < _n_time_ticks_data; tick ++) {
    _median_accumulator.clear();
    _median_accumulator.reserve(block_wire_end - block_wire_start);
    for (int wire = 0; wire < block_wire_end - block_wire_start ; wire ++) {
      if (_wire_status_by_plane[plane][wire] == kNormal) {
        offset = tick + wire * _n_time_ticks_data;
        // std::cout << "Accessing at wire " << wire << ", offset " << offset << std::endl;
        _median_accumulator.push_back(_wire_block[offset]);
        // std::cout << "Success!" << std::endl;


      }
    }

    // std::cout << "_median_accumulator.size() " << _median_accumulator.size() << std::endl;

    // Now find the median of this tick:

    if (_median_accumulator.size() < 8) {
      continue;
    }

    std::nth_element(_median_accumulator.begin(),
                     _median_accumulator.begin() + _median_accumulator.size() / 2,
                     _median_accumulator.end());
    float median = _median_accumulator.at(_median_accumulator.size() / 2);

    // std::cout << "median is " << median << std::endl;
    // if (_median_accumulator.size() % 2 == 0) {
    //   std::nth_element(_median_accumulator.begin(),
    //                    _median_accumulator.begin() + _median_accumulator.size() / 2 - 1,
    //                    _median_accumulator.end());
    //   median += _median_accumulator.at(_median_accumulator.size() / 2 - 1);
    //   median /= 2.0;
    // }
    // std::cout << "median is " << median << std::endl;

    correlatedNoiseWaveforms.at(plane).at(i_block).at(tick) = median;
    // if (tick > 3000)
    // return;

  }


}


bool UbooneNoiseFilter::is_chirping(float * _data_arr,
                                    int N,
                                    unsigned int wire,
                                    unsigned int plane) {
  // Run through the chirp filter stuff here.
  //


  // This function comes first, and determines if the waveform is chirping or not.
  if (_chirp_filter.ChirpFilterAlg(_data_arr, N)) {

    _chirp_info_by_plane[plane][wire] = _chirp_filter.get_current_chirp_info();
    _wire_status_by_plane[plane][wire] = kChirping;
    // then this channel is chirping, and we deal with it.
    // _chirp_filter.ZigzagFilterAlg(_data_arr, N);
    // _chirp_filter.RawAdaptiveBaselineAlg(_data_arr, N);
    // _chirp_filter.RemoveChannelFlags(_data_arr, N);
    return true;
  } else {
    // _chirp_filter.ZigzagFilterAlg(_data_arr, N);
    return false;

  }

}

void UbooneNoiseFilter::tag_special_wire_statuses() {

  // This function tags wires that are known to be dead, or in an otherwise
  // weird state.

  // Get the edge wires in the first plane that are very noisy
  for (int wire = 0; wire < 16; wire++) {
    _wire_status_by_plane[0][wire] = kHighNoise;
  }
  for (int wire = 2400 - 16 - 1; wire < 2400; wire++) {
    _wire_status_by_plane[0][wire] = kHighNoise;
  }
}



}

#endif
