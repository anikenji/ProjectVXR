/// High-quality fractional audio resampler using cubic Hermite spline interpolation.
/// Designed for micro-adjustments in Audio Clock Drift Compensation.
pub struct FractionalResampler;

impl FractionalResampler {
    /// Resamples interleaved stereo PCM float buffer by `ratio` (e.g. 1.0005 = speed up / more output samples, 0.9995 = slow down).
    /// Input length must be a multiple of `channels` (default 2).
    pub fn resample_stereo(input: &[f32], ratio: f64) -> Vec<f32> {
        let channels = 2;
        if input.is_empty() || (ratio - 1.0).abs() < 1e-7 {
            return input.to_vec();
        }

        let in_frames = input.len() / channels;
        if in_frames < 4 {
            return input.to_vec();
        }

        let out_frames = (in_frames as f64 * ratio).round().max(1.0) as usize;
        let mut output = Vec::with_capacity(out_frames * channels);

        let step = (in_frames - 1) as f64 / (out_frames - 1).max(1) as f64;

        for out_idx in 0..out_frames {
            let src_pos = out_idx as f64 * step;
            let i1 = src_pos.floor() as usize;
            let t = (src_pos - i1 as f64) as f32;

            let i0 = i1.saturating_sub(1);
            let i2 = (i1 + 1).min(in_frames - 1);
            let i3 = (i1 + 2).min(in_frames - 1);

            for c in 0..channels {
                let y0 = input[i0 * channels + c];
                let y1 = input[i1 * channels + c];
                let y2 = input[i2 * channels + c];
                let y3 = input[i3 * channels + c];

                // Cubic Hermite spline coefficients
                let a0 = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
                let a1 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
                let a2 = -0.5 * y0 + 0.5 * y2;
                let a3 = y1;

                let val = ((a0 * t + a1) * t + a2) * t + a3;
                output.push(val.clamp(-1.0, 1.0));
            }
        }

        output
    }

    /// Resamples an input buffer to exactly `target_total_samples`
    pub fn resample_to_size(input: &[f32], target_total_samples: usize) -> Vec<f32> {
        let channels = 2;
        if input.len() == target_total_samples || input.len() < channels * 4 {
            let mut out = input.to_vec();
            out.resize(target_total_samples, 0.0);
            return out;
        }

        let in_frames = input.len() / channels;
        let out_frames = target_total_samples / channels;
        let ratio = out_frames as f64 / in_frames as f64;

        let mut resampled = Self::resample_stereo(input, ratio);
        resampled.resize(target_total_samples, 0.0);
        resampled
    }
}
