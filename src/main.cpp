#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define ACCESS _access
#define X_OK 1
static constexpr char kPathDelim = ';';
#else
#include <unistd.h>
#include <sys/wait.h>
#define ACCESS access
static constexpr char kPathDelim = ':';
#endif

namespace fs = std::filesystem;

static bool is_executable_file(const fs::path &p)
{
	std::error_code ec;
	auto st = fs::symlink_status(p, ec);
	if (ec)
		return false;
	if (!(fs::is_regular_file(st) or fs::is_symlink(st)))
		return false;
	std::string ps = p.string();
	return ACCESS(ps.c_str(), X_OK) == 0;
}

static bool contains_slash(const std::string &s)
{
	return s.find('/') != std::string::npos or s.find('\\') != std::string::npos;
}

static std::string find_in_PATH(const std::string &cmd)
{
	// 遍历PTAH的每个目录
	const char *path_env = std::getenv("PATH");
	std::string path_list = path_env ? std::string(path_env) : std::string();
	size_t start = 0;
	while (true)
	{
		// 找到下一个分隔符
		size_t end = path_list.find(kPathDelim, start);
		// 截取目录
		std::string dir = (end == std::string::npos) ? path_list.substr(start)
													 : path_list.substr(start, end - start);
		if (dir.empty())
			dir = ".";
		fs::path candidate = fs::path(dir) / cmd;
		if (is_executable_file(candidate))
		{
			return candidate.string();
		}
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	return "";
}

static std::string get_cwd()
{
	char *cwd = getcwd(nullptr, 0);
	if (cwd)
	{
		std::string res(cwd);
		std::free(cwd); // 这块内存是libc malloc出来的，要手动free
		return res;
	}
	else
		return " "; // 获取失败
}

static std::vector<std::string> parse_line(const std::string &line)
{
	enum class Mode
	{
		OUT,
		IN_SQ,
		IN_DQ
	};
	Mode mode = Mode::OUT;
	// OUT 引号外
	//  空白分隔参数，连续空白折叠
	//  反斜杠转义下一个字符，被转义字符作为字面量

	// IN_SQ 单引号内
	// 一切都是自变量，反斜杠不转义

	// IN_DQ 双引号内
	// 字面量为主，空白保留
	// 反斜杠只转义\"和\\ ，其他反斜杠保留
	std::vector<std::string> out;
	std::string cur;
	bool token_active = false;

	for (int i = 0; i < line.size(); i++)
	{
		char ch = line[i];
		unsigned char c = static_cast<unsigned>(ch);

		if (mode == Mode::OUT)
		{
			if (ch == '\'')
			{
				mode = Mode::IN_SQ;
				token_active = true;
			}
			else if (ch == '\"')
			{
				mode = Mode::IN_DQ;
				token_active = true;
			}
			else if (ch == '\\')
			{
				if (i + 1 < line.size())
				{
					char nxt = line[i + 1];
					cur.push_back(nxt);
					token_active = true;
					i++;
				}
				else
				{
					cur.push_back('\\'); // 保守一点当字面量吧，续行太难写了
					token_active = true;
				}
			}
			else if (std::isspace(c))
			{
				if (token_active)
				{
					out.push_back(cur);
					cur.clear();
					token_active = false;
				}
			}
			else
			{
				cur.push_back(ch);
				token_active = true;
			}
		}
		else if (mode == Mode::IN_SQ)
		{
			if (ch == '\'')
				mode = Mode::OUT;
			else
			{
				cur.push_back(ch);
				token_active = true;
			}
		}
		else // mode == Mode::IN_DQ
		{
			if (ch == '\"')
				mode = Mode::OUT;
			else if (ch == '\\')
			{
				if (i + 1 < line.size())
				{
					char nxt = line[i + 1];
					if (nxt == '\\' or nxt == '\"')
					{
						cur.push_back(nxt);
						token_active = true;
						i++;
					}
					else
					{
						cur.push_back('\\');
						token_active = true;
					}
				}
				else
				{
					cur.push_back('\\'); // 保守一点当字面量吧，续行太难写了
					token_active = true;
				}
			}
			else
			{
				cur.push_back(ch);
				token_active = true;
			}
		}
	}
	// 行末仍有token
	if (token_active)
		out.push_back(cur);
	return out;
}

int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	const std::unordered_set<std::string> builtin_commands = {"echo", "exit", "type", "pwd", "cd"};

	while (true)
	{
		std::cout << "$ ";
		std::string line;
		std::getline(std::cin, line);
		std::vector<std::string> tokens = parse_line(line);

		if (tokens.empty())
			continue;
		std::string command = tokens[0];
		if (command == "exit")
			return 0;
		else if (command == "cd")
		{
			if (tokens.size() < 2)
				continue; // 缺少参数
			std::string &target = tokens[1];
			if (target == "~")
			{
				const char *home = std::getenv("HOME");
				// 先判断home不是空指针，然后判断*home不是空字符串
				if (home and *home)
					target = home;
				else
				{
					std::cout << "cd: ~: No such file or directory" << std::endl;
					continue;
				}
			}

			std::string old_cwd = get_cwd();

			if (chdir(target.c_str()) != 0)
			{
				if (!old_cwd.empty())
					(void)chdir(old_cwd.c_str());
				std::cout << "cd: " << target << ": No such file or directory" << std::endl;
			}
		}
		else if (command == "pwd")
		{
			std::string cwd = get_cwd();
			std::cout << cwd << std::endl;
		}
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
			// 无第二个 token
			if (tokens.size() < 2)
			{
				std::cout << ": not found" << std::endl;
				continue;
			}

			std::string q = tokens[1];
			// 先判断是否是 buildin
			if (builtin_commands.count(q))
			{
				std::cout << q << " is a shell builtin" << std::endl;
				continue;
			}

			// 若为路径
			if (contains_slash(q))
			{
				fs::path p(q);
				if (is_executable_file(p))
				{
					std::cout << q << " is " << p.string() << std::endl;
				}
				else
					std::cout << q << ": not found" << std::endl;
			}

			std::string res = find_in_PATH(q);
			if (!res.empty())
			{
				std::cout << q << " is " << res << std::endl;
			}
			else
			{
				std::cout << q << ": not found\n";
			}
		}
		else
		{
			const std::string &cmd = tokens[0];
			std::string full_path = find_in_PATH(cmd);
			if (full_path.empty())
			{
				std::cout << command << ": command not found" << std::endl;
				continue;
			}
			// 这里fork创建了一个一模一样的进程
			// 子进程返回0，表示自己是子进程
			// 父进程返回子进程PID(>0)
			pid_t pid = fork();
			if (pid < 0) // fork返回负值，创建子进程发生错误
			{
				std::cerr << "fork failed" << std::endl;
				continue;
			}
			if (pid == 0) // 子进程
			{
				std::vector<char *> argv;
				argv.reserve(tokens.size() + 1);
				for (auto &t : tokens)
					argv.push_back(const_cast<char *>(t.c_str()));
				argv.push_back(nullptr);
				execv(full_path.c_str(), argv.data());
				// execv失败，没有进入新程序
				std::cerr << cmd << ": exec failed\n";
				_exit(1);
			}
			else // 父进程
			{
				int status = 0;
				(void)waitpid(pid, &status, 0);
			}
		}
	}
}
