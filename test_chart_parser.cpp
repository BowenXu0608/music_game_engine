#include <iostream>
#include "game/chart/ChartLoader.h"

int main() {
    try {
        // Test Bandori parser
        std::cout << "Testing Bandori parser...\n";
        auto bandori = ChartLoader::load("../../test_charts/bandori_demo.json");
        std::cout << "  Title: " << bandori.title << "\n";
        std::cout << "  Artist: " << bandori.artist << "\n";
        std::cout << "  Notes: " << bandori.notes.size() << "\n";

        // Test Arcaea parser
        std::cout << "\nTesting Arcaea parser...\n";
        auto arcaea = ChartLoader::load("../../test_charts/arcaea_demo.aff");
        std::cout << "  Title: " << arcaea.title << "\n";
        std::cout << "  Notes: " << arcaea.notes.size() << "\n";

        // Test Cytus parser
        std::cout << "\nTesting Cytus parser...\n";
        auto cytus = ChartLoader::load("../../test_charts/cytus_demo.xml");
        std::cout << "  Title: " << cytus.title << "\n";
        std::cout << "  Notes: " << cytus.notes.size() << "\n";

        // Test Lanota parser
        std::cout << "\nTesting Lanota parser...\n";
        auto lanota = ChartLoader::load("../../test_charts/lanota_demo.lan");
        std::cout << "  Title: " << lanota.title << "\n";
        std::cout << "  Notes: " << lanota.notes.size() << "\n";

        std::cout << "\nAll parsers working!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
