#pragma once

#include <algorithm>
#include <cmath>

namespace espp {
  /**
    * @brief Template class for converting a value from an uncentered [minimum,
    *        maximum] range into a centered output range (default [-1,1]). If
    *        provided a non-zero deadband, it will convert all values within
    *        [center-deadband, center+deadband] to be the configured
    *        output_center (default 0).
    *
    *        The RangeMapper can be optionally configured to invert the input,
    *        so that it will compute the input w.r.t. the configured min/max of
    *        the input range when mapping to the output range - this will mean
    *        that a values within the ranges [minimum, minimum+deadband] and
    *        [maximum-deadband, maximum] will all map to the output_center and
    *        the input center will map to both output_max and output_min
    *        depending on the sign of the input.
    *
    * @note When inverting the input range, you are introducing a discontinuity
    *       between the input distribution and the output distribution at the
    *       input center. Noise around the input's center value will create
    *       oscillations in the output which will jump between output maximum
    *       and output minimum. Therefore it is advised to use \p invert_input
    *       sparignly, and to set the values robustly.
    */
  template<typename T> class RangeMapper {
  public:
    /**
      *  @brief Configuration for the input uncentered range with optional
      *  values for the centered output range, default values of 0 output center
      *  and 1 output range provide a default output range between [-1, 1].
      */
    struct Config {
      T center;  /**< Center value for the input range. */
      T deadband;/**< Deadband amount around (+-) the center for which output will be 0. */
      T minimum; /**< Minimum value for the input range. */
      T maximum; /**< Maximum value for the input range. */
      bool invert_input{false}; /**< Whether to invert the input distribution (default false). @note If true will compute the input relative to min/max instead of to center. */
      T output_center{T(0)}; /**< The center for the output. Default 0. */
      T output_range{T(1)}; /**< The range (+/-) from the center for the output. Default 1. @note Will be passed through std::abs() to ensure it is positive. */
    };

    /**
     * @brief Initialize the RangeMapper.
     * @param config Configuration describing the input distribution.
     */
    RangeMapper(const Config& config) {
      configure(config);
    }

    /**
     * @brief Update the input / output distribution with the new configuration.
     * @param config New configuration to use.
     */
    void configure(const Config& config) {
      assert(config.output_range != T(0));
      center_ = config.center;
      deadband_ = config.deadband;
      minimum_ = config.minimum;
      maximum_ = config.maximum;
      invert_input_ = config.invert_input;
      output_center_ = config.output_center;
      output_range_ = std::abs(config.output_range);
      output_min_ = output_center_ - output_range_;
      output_max_ = output_center_ + output_range_;
      pos_range_ = (maximum_ - center_) / output_range_;
      neg_range_ = std::abs(minimum_ - center_) / output_range_;
    }

    /**
     * @brief Return the configured center of the output distribution
     * @return Center of the output distribution for this range mapper.
     */
    T get_output_center() const { return output_center_; }

    /**
     * @brief Return the configured range of the output distribution
     * @note Always positive.
     * @return Range of the output distribution for this range mapper.
     */
    T get_output_range() const { return output_range_; }

    /**
     * @brief Return the configured minimum of the output distribution
     * @return Minimum of the output distribution for this range mapper.
     */
    T get_output_min() const { return output_min_; }

    /**
     * @brief Return the configured maximum of the output distribution
     * @return Maximum of the output distribution for this range mapper.
     */
    T get_output_max() const { return output_max_; }

    /**
     * @brief Map a value \p v from the input distribution into the configured
     *        output range (centered, default [-1,1]).
     * @param v Value from the (possibly uncentered and possibly inverted -
     *        defined by the previously configured Config) input distribution
     * @return Value within the centered output distribution.
     */
    T map(const T& v) {
      T clamped = std::clamp(v, minimum_, maximum_);
      T calibrated{0};
      if (invert_input_) {
        // if we invert the input, then we are comparing against the min/max
        calibrated = clamped >= T(0) ?
          maximum_ - clamped
          :
          minimum_ - clamped;
      } else {
        // normally we compare against center
        calibrated = clamped - center_;
      }
      if (std::abs(calibrated) < deadband_) {
        return output_center_;
      }
      return (calibrated >= T(0)) ?
        calibrated / pos_range_ + output_center_ : calibrated / neg_range_ + output_center_;
    }

  protected:
    T center_;
    T deadband_;
    T minimum_;
    T maximum_;
    bool invert_input_;
    T pos_range_;
    T neg_range_;
    T output_center_;
    T output_range_;
    T output_min_;
    T output_max_;
  };

  /**
   * @brief Typedef for float ranges.
   */
  typedef RangeMapper<float> FloatRangeMapper;

  /**
   * @brief Typedef for int ranges.
   */
  typedef RangeMapper<int> IntRangeMapper;
}
