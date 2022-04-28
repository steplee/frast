
namespace {

// ------------------------------------------
//   Definitions
// ------------------------------------------

const double R1         = (6378137.0);
const double R2         = (6356752.314245179);
const double R1_inv     = (1. / 6378137.0);
const double WGS84_F    = (1. / 298.257223563);
const double WGS84_D    = (R2 / R1);
const double a          = 1;
const double b          = R2 / R1;
const double a2         = a * a;
const double b2         = b * b;
const double e2         = 1 - (b * b / a * a);
const double ae2        = a * e2;
const double b2_over_a2 = b2 / a2;
const double one_div_pi = 1 / M_PI;

constexpr double WebMercatorScale = 20037508.342789248;


void geodetic_to_ecef(double* out, int n, const double* llh) {
    for (int i = 0; i < n; i++) {
        const double aa = llh[i * 3 + 0], bb = llh[i * 3 + 1], cc = llh[i * 3 + 2];
        double       cos_phi = std::cos(bb), cos_lamb = std::cos(aa);
        double       sin_phi = std::sin(bb), sin_lamb = std::sin(aa);
        double       n_phi = a / std::sqrt(1 - e2 * sin_phi * sin_phi);

        out[i * 3 + 0] = (n_phi + cc) * cos_phi * cos_lamb;
        out[i * 3 + 1] = (n_phi + cc) * cos_phi * sin_lamb;
        out[i * 3 + 2] = (b2_over_a2 * n_phi + cc) * sin_phi;
    }
}

void unit_wm_to_geodetic(double* out, int n, const double* xyz) {
    for (int i = 0; i < n; i++) {
        out[i * 3 + 0] = xyz[i * 3 + 0] * M_PI;
        out[i * 3 + 1] = std::atan(std::exp(xyz[i * 3 + 1] * M_PI)) * 2 - M_PI / 2;
        out[i * 3 + 2] = xyz[i * 3 + 2] * M_PI;
    }
}

void unit_wm_to_ecef(double* out, int n, const double* xyz) {
    // OKAY: both unit_wm_to_geodetic and geodetic_to_ecef can operate in place.
    unit_wm_to_geodetic(out, n, xyz);
    geodetic_to_ecef(out, n, out);
}




// float32
void geodetic_to_ecef(float* out, int n, const float* llh, int stride=3) {
    for (int i = 0; i < n; i++) {
        const float aa = llh[i * stride + 0], bb = llh[i * stride + 1], cc = llh[i * stride + 2];
        float       cos_phi = std::cos(bb), cos_lamb = std::cos(aa);
        float       sin_phi = std::sin(bb), sin_lamb = std::sin(aa);
        float       n_phi = a / std::sqrt(1 - static_cast<float>(e2) * sin_phi * sin_phi);

        out[i * stride + 0] = (n_phi + cc) * cos_phi * cos_lamb;
        out[i * stride + 1] = (n_phi + cc) * cos_phi * sin_lamb;
        out[i * stride + 2] = (static_cast<float>(b2_over_a2) * n_phi + cc) * sin_phi;
    }
}

void unit_wm_to_geodetic(float* out, int n, const float* xyz, int stride=3) {
    for (int i = 0; i < n; i++) {
        out[i * stride + 0] = xyz[i * stride + 0] * static_cast<float>(M_PI);
        out[i * stride + 1] = std::atan(std::exp(xyz[i * stride + 1] * static_cast<float>(M_PI))) * 2 - static_cast<float>(M_PI) / 2;
        out[i * stride + 2] = xyz[i * stride + 2] * static_cast<float>(M_PI);
    }
}

void unit_wm_to_ecef(float* out, int n, const float* xyz, int stride=3) {
    // OKAY: both unit_wm_to_geodetic and geodetic_to_ecef can operate in place.
    unit_wm_to_geodetic(out, n, xyz, stride);
    geodetic_to_ecef(out, n, out, stride);
}

}
