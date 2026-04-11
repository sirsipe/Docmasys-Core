#include "cli/Commands.hpp"

#include <exception>
#include <iostream>

int main(int argc, char *argv[])
{
  try
  {
    return Docmasys::CLI::Dispatch(argc, argv);
  }
  catch (const std::exception &ex)
  {
    std::cerr << "error: " << ex.what() << '\n';
    return 1;
  }
}
