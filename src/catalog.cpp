#include "catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSunGalacticRadiusPc = 8200.0;
constexpr double kSunHeightPc = 20.8;
constexpr double kCircularSpeedKmPerSec = 233.0;
constexpr double kKmPerSecToPcPerYear = 1.022712165045695e-6;

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string lowerCopy(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string current;
    bool inQuotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];

        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
        } else if (c == ',' && !inQuotes) {
            out.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }

    out.push_back(trim(current));
    return out;
}

double toDouble(const std::string& text, double fallback = 0.0) {
    try {
        const std::string cleaned = trim(text);
        if (cleaned.empty()) {
            return fallback;
        }
        return std::stod(cleaned);
    } catch (...) {
        return fallback;
    }
}

std::uint32_t makeStableId(std::size_t rowIndex, const std::string& preferred) {
    try {
        const unsigned long long parsed = std::stoull(preferred);
        return static_cast<std::uint32_t>(parsed & 0xffffffffu);
    } catch (...) {
        return static_cast<std::uint32_t>((rowIndex + 1) & 0xffffffffu);
    }
}

Vec3 circularVelocityAroundGalacticCenter(const Vec3& galPosPc) {
    const double radius = std::sqrt(galPosPc.x * galPosPc.x + galPosPc.y * galPosPc.y);
    if (radius < 1e-9) {
        return {0.0, 0.0, 0.0};
    }

    const double speed = kCircularSpeedKmPerSec * kKmPerSecToPcPerYear;
    const double tx = galPosPc.y / radius;
    const double ty = -galPosPc.x / radius;
    return {tx * speed, ty * speed, 0.0};
}

bool isGaiaHeader(const std::unordered_map<std::string, std::size_t>& index) {
    return index.count("l") && index.count("b") && index.count("parallax") &&
           index.count("phot_g_mean_mag");
}

bool isCartesianHeader(const std::unordered_map<std::string, std::size_t>& index) {
    return index.count("x") && index.count("y") && index.count("z");
}

double bodyDistancePc(const Body& body) {
    return std::sqrt(body.pos.x * body.pos.x + body.pos.y * body.pos.y + body.pos.z * body.pos.z);
}

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

std::uint64_t stableSeedFromBody(const Body& body, std::size_t rowIndex) {
    try {
        if (!body.sourceId.empty()) {
            return splitmix64(static_cast<std::uint64_t>(std::stoull(body.sourceId)));
        }
    } catch (...) {
    }
    return splitmix64(static_cast<std::uint64_t>(rowIndex + 1));
}

double deterministicUnitRandom(const Body& body, std::size_t rowIndex) {
    const std::uint64_t bits = stableSeedFromBody(body, rowIndex);
    constexpr double kInvMax = 1.0 / static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<double>(bits) * kInvMax;
}

double distanceBiasedAcceptanceProbability(const Body& body, double sampleFraction) {
    const double distancePc = bodyDistancePc(body);
    // Bias toward distant stars from gaia_new.csv while keeping the average near the target fraction.
    const double farScore = distancePc / (distancePc + 800.0);
    const double weight = 0.5 + farScore; // range ~[0.5, 1.5)
    return std::min(1.0, sampleFraction * weight);
}

Body makeBodyFromGaia(const std::vector<std::string>& row,
                      const std::unordered_map<std::string, std::size_t>& index,
                      std::size_t rowIndex) {
    const double lDeg = toDouble(row[index.at("l")], std::numeric_limits<double>::quiet_NaN());
    const double bDeg = toDouble(row[index.at("b")], std::numeric_limits<double>::quiet_NaN());
    const double parallaxMas =
        toDouble(row[index.at("parallax")], std::numeric_limits<double>::quiet_NaN());

    if (!std::isfinite(lDeg) || !std::isfinite(bDeg) || !std::isfinite(parallaxMas) ||
        parallaxMas <= 0.0) {
        Body invalid;
        invalid.pos.x = std::numeric_limits<double>::quiet_NaN();
        return invalid;
    }

    const double distancePc = 1000.0 / parallaxMas;
    const double l = lDeg * (kPi / 180.0);
    const double b = bDeg * (kPi / 180.0);

    const double cosB = std::cos(b);
    const Vec3 heliocentricPos{
        distancePc * cosB * std::cos(l),
        distancePc * cosB * std::sin(l),
        distancePc * std::sin(b)};

    const Vec3 sunGalPos{-kSunGalacticRadiusPc, 0.0, kSunHeightPc};
    const Vec3 starGalPos = sunGalPos + heliocentricPos;
    const Vec3 starGalVel = circularVelocityAroundGalacticCenter(starGalPos);
    const Vec3 sunGalVel = circularVelocityAroundGalacticCenter(sunGalPos);

    Body body;
    const auto sourceIt = index.find("source_id");
    const std::string sourceId = (sourceIt != index.end()) ? row[sourceIt->second] : "";
    body.id = makeStableId(rowIndex, sourceId);
    body.sourceId = sourceId;
    // name is resolved externally from the crossmatch / catalog lookup tables
    body.pos = heliocentricPos;
    body.vel = starGalVel - sunGalVel;
    body.mag = static_cast<float>(toDouble(row[index.at("phot_g_mean_mag")], 15.0));

    const auto colorIt = index.find("bp_rp");
    body.colorIndex = static_cast<float>(
        colorIt != index.end() ? toDouble(row[colorIt->second], 0.8) : 0.8);

    body.labelDebugReason = "unresolved";
    body.isSun = false;
    body.hasMotion = true;
    return body;
}

// ── Name-lookup helpers ──────────────────────────────────────────────────────

struct CatalogNameInfo {
    int hip       = 0;
    int hd        = 0;
    int flamsteed = 0;
    std::string bayer;
    std::string cst;
    std::string simbadName;
};

// names.csv: source_id,hip
void loadSourceToHip(const std::string& path,
                     std::unordered_map<long long, int>& out)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    if (!std::getline(f, line)) return;

    const auto header = parseCsvLine(line);
    int sourceCol = -1;
    int hipCol = -1;
    for (std::size_t i = 0; i < header.size(); ++i) {
        const std::string key = lowerCopy(trim(header[i]));
        if (key == "source_id") {
            sourceCol = static_cast<int>(i);
        } else if (key == "hip" || key == "original_ext_source_id") {
            hipCol = static_cast<int>(i);
        }
    }

    if (sourceCol < 0 || hipCol < 0) {
        return;
    }

    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto cols = parseCsvLine(line);
        const std::size_t sourceIdx = static_cast<std::size_t>(sourceCol);
        const std::size_t hipIdx = static_cast<std::size_t>(hipCol);
        if (cols.size() <= sourceIdx || cols.size() <= hipIdx ||
            cols[sourceIdx].empty() || cols[hipIdx].empty()) {
            continue;
        }
        try {
            out[std::stoll(cols[sourceIdx])] = std::stoi(cols[hipIdx]);
        } catch (...) {}
    }
}

// iv27a_catalog_clean.csv: n_HD,HD,DM,GC,HR,HIP,RAJ2000,DEJ2000,Vmag,Fl,Bayer,Cst,SimbadName
void loadHipCatalog(const std::string& path,
                    std::unordered_map<int, CatalogNameInfo>& out)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto cols = parseCsvLine(line);
        if (cols.size() < 13 || cols[5].empty()) continue;
        try {
            CatalogNameInfo info;
            info.hip = std::stoi(cols[5]);
            if (!cols[1].empty()) { try { info.hd  = std::stoi(cols[1]); } catch (...) {} }
            if (!cols[9].empty()) { try { info.flamsteed = std::stoi(cols[9]); } catch (...) {} }
            info.bayer      = cols[10];
            info.cst        = cols[11];
            info.simbadName = cols[12];
            out[info.hip] = info;
        } catch (...) {}
    }
}

// iv27a_table3_clean.csv: HD,BFD,Name,r_Name  (multiple rows per HD – keep first)
void loadHdProperNames(const std::string& path,
                       std::unordered_map<int, std::string>& out)
{
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto cols = parseCsvLine(line);
        if (cols.size() < 3 || cols[0].empty() || cols[2].empty()) continue;
        try {
            int hd = std::stoi(cols[0]);
            std::string name = cols[2];
            out.emplace(hd, name); // emplace: keeps first (preferred) name per HD
        } catch (...) {}
    }
}

std::string resolveStarName(
    long long sourceId,
    const std::unordered_map<long long, int>& sourceToHip,
    const std::unordered_map<int, CatalogNameInfo>& hipToCatalog,
    const std::unordered_map<int, std::string>& hdToProperName,
    std::string* reason = nullptr)
{
    auto hipIt = sourceToHip.find(sourceId);
    if (hipIt == sourceToHip.end()) {
        if (reason) *reason = "missing_source_to_hip";
        return "";
    }

    const int hip = hipIt->second;

    auto catIt = hipToCatalog.find(hip);
    if (catIt == hipToCatalog.end()) {
        if (reason) *reason = "hip_fallback";
        return "";
    }

    const CatalogNameInfo& info = catIt->second;

    // 1. Proper name via HD
    if (info.hd > 0) {
        auto properIt = hdToProperName.find(info.hd);
        if (properIt != hdToProperName.end() && !properIt->second.empty()) {
            if (reason) *reason = "proper_name";
            return properIt->second;
        }
    }

    // 2. SIMBAD-style designation
    if (!info.simbadName.empty()) {
        if (reason) *reason = "simbad_name";
        return info.simbadName;
    }

    // 3. Bayer + constellation
    if (!info.bayer.empty() && !info.cst.empty()) {
        if (reason) *reason = "bayer_constellation";
        return info.bayer + " " + info.cst;
    }

    // 4. Flamsteed + constellation
    if (info.flamsteed > 0 && !info.cst.empty()) {
        if (reason) *reason = "flamsteed_constellation";
        return std::to_string(info.flamsteed) + " " + info.cst;
    }

    // 5. HIP fallback
    if (reason) *reason = "hip_fallback";
    return "";
}

// ── Cartesian body ────────────────────────────────────────────────────────────

Body makeBodyFromCartesian(const std::vector<std::string>& row,
                           const std::unordered_map<std::string, std::size_t>& index,
                           std::size_t rowIndex) {
    Body body;

    const auto idIt = index.find("id");
    const std::string sourceId = idIt != index.end() ? row[idIt->second] : "";
    body.id = makeStableId(rowIndex, sourceId);
    body.sourceId = sourceId;

    const auto nameIt = index.find("name");
    body.name = (nameIt != index.end() && !trim(row[nameIt->second]).empty())
                    ? trim(row[nameIt->second])
                    : ("Star " + std::to_string(rowIndex + 1));

    body.pos.x = toDouble(row[index.at("x")], 0.0);
    body.pos.y = toDouble(row[index.at("y")], 0.0);
    body.pos.z = toDouble(row[index.at("z")], 0.0);

    const auto vxIt = index.find("vx");
    const auto vyIt = index.find("vy");
    const auto vzIt = index.find("vz");
    body.vel.x = vxIt != index.end() ? toDouble(row[vxIt->second], 0.0) : 0.0;
    body.vel.y = vyIt != index.end() ? toDouble(row[vyIt->second], 0.0) : 0.0;
    body.vel.z = vzIt != index.end() ? toDouble(row[vzIt->second], 0.0) : 0.0;

    const auto magIt = index.find("mag");
    body.mag = static_cast<float>(magIt != index.end() ? toDouble(row[magIt->second], 10.0) : 10.0);

    const auto ciIt = index.find("ci");
    body.colorIndex = static_cast<float>(ciIt != index.end() ? toDouble(row[ciIt->second], 0.8) : 0.8);

    body.labelDebugReason = "cartesian_input";
    body.isSun = false;
    body.hasMotion = true;
    return body;
}
} // namespace

bool Catalog::loadCsv(const std::string& path, double sampleFraction, bool append) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV file: " << path << "\n";
        return false;
    }

    if (!append) {
        bodies_.clear();
    }

    if (!(sampleFraction > 0.0)) {
        std::cerr << "Sample fraction must be > 0 for CSV file: " << path << "\n";
        return false;
    }
    if (sampleFraction > 1.0) {
        sampleFraction = 1.0;
    }

    std::string headerLine;
    if (!std::getline(file, headerLine)) {
        std::cerr << "CSV file is empty: " << path << "\n";
        return false;
    }

    const std::vector<std::string> headerParts = parseCsvLine(headerLine);
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < headerParts.size(); ++i) {
        index.emplace(lowerCopy(trim(headerParts[i])), i);
    }

    const bool gaiaFormat = isGaiaHeader(index);
    const bool cartesianFormat = isCartesianHeader(index);

    if (!gaiaFormat && !cartesianFormat) {
        std::cerr << "Unsupported CSV header. Expected either:\n"
                  << "  Cartesian: id,name,x,y,z,vx,vy,vz,mag,ci\n"
                  << "  Gaia: source_id,l,b,parallax,phot_g_mean_mag,bp_rp\n";
        return false;
    }

    // --- Name-lookup tables (Gaia format only) --------------------------------
    std::unordered_map<long long, int>         sourceToHip;
    std::unordered_map<int, CatalogNameInfo>   hipToCatalog;
    std::unordered_map<int, std::string>       hdToProperName;

    if (gaiaFormat) {
        // Compute directory of the input CSV so sibling files are found correctly.
        const auto sepPos = path.find_last_of("/\\");
        const std::string dir = (sepPos == std::string::npos) ? "." : path.substr(0, sepPos);

        loadSourceToHip  (dir + "/names.csv",               sourceToHip);
        loadHipCatalog   (dir + "/iv27a_catalog_clean.csv", hipToCatalog);
        loadHdProperNames(dir + "/iv27a_table3_clean.csv",  hdToProperName);

        std::cout << "Name lookup tables: "
                  << sourceToHip.size()    << " Gaia-HIP, "
                  << hipToCatalog.size()   << " HIP-catalog, "
                  << hdToProperName.size() << " HD-proper\n";
    }
    // -------------------------------------------------------------------------

    const auto sourceIdColIt = index.find("source_id");
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist01(0.0, 1.0);

    std::string line;
    std::size_t rowIndex = 0;
    std::size_t skipped  = 0;
    std::size_t named    = 0;
    std::size_t sourceMatched = 0;
    std::size_t catalogMatched = 0;
    std::size_t properMatched = 0;
    std::size_t sampledOut = 0;

    while (std::getline(file, line)) {
        if (trim(line).empty()) {
            continue;
        }

        std::vector<std::string> row = parseCsvLine(line);
        row.resize(headerParts.size());

        Body body = gaiaFormat ? makeBodyFromGaia(row, index, rowIndex)
                               : makeBodyFromCartesian(row, index, rowIndex);

        const bool validPosition = std::isfinite(body.pos.x) && std::isfinite(body.pos.y) &&
                                   std::isfinite(body.pos.z);
        // For Gaia stars, allow empty names (unlabelled stars still render).
        // For Cartesian stars, skip nameless non-sun bodies (shouldn't happen).
        if (!validPosition || (!gaiaFormat && !body.isSun && body.name.empty())) {
            ++skipped;
            ++rowIndex;
            continue;
        }

        if (sampleFraction < 1.0) {
            const double acceptProbability = distanceBiasedAcceptanceProbability(body, sampleFraction);
            const double draw = dist01(rng);
            if (draw > acceptProbability) {
                ++sampledOut;
                ++rowIndex;
                continue;
            }
        }

        // Resolve proper name from lookup tables (Gaia format only)
        if (gaiaFormat && sourceIdColIt != index.end()) {
            const std::string& sidStr = row[sourceIdColIt->second];
            if (!sidStr.empty()) {
                try {
                    const long long sid = std::stoll(sidStr);
                    auto hipIt = sourceToHip.find(sid);
                    if (hipIt != sourceToHip.end()) {
                        ++sourceMatched;
                        if (hipToCatalog.find(hipIt->second) != hipToCatalog.end()) {
                            ++catalogMatched;
                        }
                    }
                    body.name = resolveStarName(
                        sid,
                        sourceToHip,
                        hipToCatalog,
                        hdToProperName,
                        &body.labelDebugReason);
                    if (!body.name.empty()) {
                        ++named;
                        ++properMatched;
                    }
                } catch (...) {
                    body.labelDebugReason = "invalid_source_id";
                }
            } else {
                body.labelDebugReason = "missing_source_id";
            }
        }

        if (!gaiaFormat && body.name.empty()) {
            body.name = "Star " + std::to_string(rowIndex + 1);
        }

        bodies_.push_back(std::move(body));
        ++rowIndex;
    }

    std::cout << "Loaded " << bodies_.size() << " stars from " << path;
    if (gaiaFormat) {
        std::cout << " (Gaia galactic coordinates -> heliocentric Cartesian parsecs)";
        std::cout << ", " << named << " named";
        std::cout << " [source->hip " << sourceMatched
                  << ", hip->catalog " << catalogMatched
                  << ", non-HIP labels " << properMatched << "]";
    }
    if (skipped > 0) {
        std::cout << ", skipped " << skipped << " invalid rows";
    }
    if (sampledOut > 0) {
        std::cout << ", sampled out " << sampledOut << " rows";
    }
    std::cout << "\n";

    return !bodies_.empty();
}
