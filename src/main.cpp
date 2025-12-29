#include <iostream>
#include <string>
#include <vector>
#include <sstream>

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	while (true)
	{
		std::cout << "$ ";
		std::string line;
		std::getline(std::cin, line);
		std::istringstream iss(line);
		std::vector<std::string> tokens;

		for (std::string s; iss >> s;)
			tokens.push_back(s);
		if (tokens.empty())
			continue;
		std::string command = tokens[0];
		if (command == "exit")
			break;
		else if (command == "echo")
		{
			for (size_t i = 1; i < tokens.size(); i++)
			{
				if (i > 1)
					std::cout << " ";
				std::cout << tokens[i];
			}
			std::cout << std::endl;
		}
		else
			std::cout << command << ": command not found" << std::endl;
	}
}
