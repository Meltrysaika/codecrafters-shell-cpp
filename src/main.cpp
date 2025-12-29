#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	const std::unordered_set<std::string> builtin_commands = {"echo", "exit", "type"};

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
		else if (command == "type")
		{
			// 期望格式 type <command>
			if (tokens.size() >= 2)
			{
				std::string q = tokens[1];
				if (builtin_commands.count(q))
					std::cout << q << " is a shell builtin" << std::endl;
				else
					std::cout << q << ": not found" << std::endl;
			}
			// 否则直接输出 not found
			else
				std::cout << ": not found" << std::endl;
		}
		else
			std::cout << command << ": command not found" << std::endl;
	}
}
