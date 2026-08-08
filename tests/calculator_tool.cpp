#include <iostream>
#include <string>
#include "src/tools/calculator_tool.h"

int main(){
    std::cout << " === calculator tool === \n ";

    std::string promts = "((3^2 - 5 + 2) / \\sqrt{16}) + \\sin(\\frac{\\pi}{6}) - \\cos(\\frac{\\pi}{3}) + \\tan(\\frac{\\pi}{4}) + \\ln(e^3)";

    CalculatorTool caltool;
    std::string test = (caltool.execute(promts)).value();
    std::cout << "test: " << test << std::endl;
    return 0;
}