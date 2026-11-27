#include "ieim_model.hpp"
#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << "Usage: example_point PATH_TO_MODEL_DATA [UT]\n";
            return 1;
        }
        const ieim::Model model(argv[1]); // Load once and reuse for many points.
        const ieim::Request point{2024, 1, 40.7, 116.4, 39.9};
        const double ut = argc == 3 ? ieim::number(argv[2]) : 12.5;
        std::cout << std::setprecision(17) << ut << '\t'
                  << std::fixed << std::setprecision(2) << model.predict_at(point, ut) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
