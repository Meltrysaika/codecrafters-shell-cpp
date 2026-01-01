#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_set>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <cctype>

#include <readline/history.h>
#include <readline/readline.h>

#include "trie.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define ACCESS _access
#define X_OK 1
static constexpr char kPathDelim = ';';
#else
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#define ACCESS access
static constexpr char kPathDelim = ':';
#endif

static constexpr char Bell = '\x07'; // 响铃

namespace fs = std::filesystem;

enum class Token_Kind
{
	Word,
	Trunc_Stdout,  // >
	Append_Stdout, // >>
	Trunc_Stderr,  // 2>
	Append_Stderr, // 2>>
};
struct Token
{
	Token_Kind kind;
	std::string text;
};
static bool is_all_digits(const std::string &s)
{
	if (s.empty())
		return false;
	for (unsigned char c : s)
		if (!std::isdigit(c))
			return false;
	return true;
}

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
		return ""; // 获取失败
}

static std::vector<Token> parse_line(const std::string &line)
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
	std::vector<Token> out;
	std::string cur;
	bool token_active = false;

	auto flush_word_if_any = [&]() -> void
	{
		if (token_active)
		{
			out.push_back(Token(Token_Kind::Word, cur));
			cur.clear();
			token_active = false;
		}
	};

	for (int i = 0; i < line.size(); i++)
	{
		char ch = line[i];
		unsigned char c = static_cast<unsigned char>(ch);

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
			else if (ch == '>')
			{
				bool is_append = (i + 1 < line.size() and line[i + 1] == '>');
				if (is_append)
					i++;

				bool is_fd1 = token_active and is_all_digits(cur) and cur == "1";
				bool is_fd2 = token_active and is_all_digits(cur) and cur == "2";
				if (is_fd1)
				{
					cur.clear();
					token_active = false;
					out.push_back(Token(is_append ? Token_Kind::Append_Stdout : Token_Kind::Trunc_Stdout,
										is_append ? "1>>" : "1>"));
				}
				else if (is_fd2)
				{
					cur.clear();
					token_active = false;
					out.push_back(Token(is_append ? Token_Kind::Append_Stderr : Token_Kind::Trunc_Stderr,
										is_append ? "2>>" : "2>"));
				}
				else
				{
					flush_word_if_any();
					token_active = false;
					out.push_back(Token(is_append ? Token_Kind::Append_Stdout : Token_Kind::Trunc_Stdout,
										is_append ? ">>" : ">"));
				}
			}
			else if (std::isspace(c))
				flush_word_if_any();
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
						cur.push_back('\\'); // 保守一点当字面量吧，续行太难写了
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
	flush_word_if_any();
	return out;
}

static void extract_redirections(std::vector<Token> &tokens,
								 std::optional<std::string> &stdout_path,
								 bool &stdout_append,
								 std::optional<std::string> &stderr_path,
								 bool &stderr_append)
{
	for (size_t i = 0; i < tokens.size();)
	{
		Token_Kind kind = tokens[i].kind;
		bool is_stdout = (kind == Token_Kind::Trunc_Stdout or kind == Token_Kind::Append_Stdout);
		bool is_stderr = (kind == Token_Kind::Trunc_Stderr or kind == Token_Kind::Append_Stderr);

		if ((is_stderr or is_stdout) and i + 1 < tokens.size() and tokens[i + 1].kind == Token_Kind::Word)
		{
			std::string path = tokens[i + 1].text;
			if (is_stdout)
			{
				stdout_path = path;
				stdout_append = (kind == Token_Kind::Append_Stdout);
			}
			else // is_stderr
			{
				stderr_path = path;
				stderr_append = (kind == Token_Kind::Append_Stderr);
			}
			tokens.erase(tokens.begin() + static_cast<size_t>(i), tokens.begin() + static_cast<size_t>(i + 2));
			continue;
		}
		i++;
	}
}

static std::vector<std::string> tokens_to_words(const std::vector<Token> &tokens)
{
	std::vector<std::string> words;
	for (const auto &tk : tokens)
		if (tk.kind == Token_Kind::Word)
			words.push_back(tk.text);
	return words;
}

static int open_file_trunc(const std::string &path)
{
	return open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
}
static int open_file_append(const std::string &path)
{
	return open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
}

struct Stdout_Rirect_Guard // 临时重定向stdout
{
	int saved_fd = -1;
	bool active = false;

	bool redirect_to(const std::string &path, bool append)
	{
		int fd = append ? open_file_append(path) : open_file_trunc(path);
		if (fd < 0)
			return false;
		saved_fd = dup(STDOUT_FILENO); // 备份当前 stdout
		dup2(fd, STDOUT_FILENO);	   // 让 stdout 指向文件
		close(fd);					   // 重定向后，fd不用再保留，所有写stdout的东西都会写入文件
		active = true;
		return true;
	}
	~Stdout_Rirect_Guard()
	{
		if (active and saved_fd >= 0)
		{
			std::cout.flush();
			dup2(saved_fd, STDOUT_FILENO);
			close(saved_fd);
		}
	}
};

struct Stderr_Rirect_Guard // 临时重定向stderr
{
	int saved_fd = -1;
	bool active = false;

	bool redirect_to(const std::string &path, bool append)
	{
		int fd = append ? open_file_append(path) : open_file_trunc(path);
		if (fd < 0)
			return false;
		saved_fd = dup(STDERR_FILENO); // 备份当前 stderr
		dup2(fd, STDERR_FILENO);	   // 让 stderr 指向文件
		close(fd);					   // 重定向后，fd不用再保留，所有写stderr的东西都会写入文件
		active = true;
		return true;
	}
	~Stderr_Rirect_Guard()
	{
		if (active and saved_fd >= 0)
		{
			std::cerr.flush();
			dup2(saved_fd, STDERR_FILENO);
			close(saved_fd);
		}
	}
};

static const std::unordered_set<std::string> builtin_commands = {"echo", "exit", "type", "pwd", "cd"};

struct AutoCompletionCache
{
	Trie trie;
	std::string cached_path;
	std::vector<std::string> dirs;
	std::vector<std::optional<fs::file_time_type>> dir_last_write_time;
	std::unordered_set<std::string> seen; // 去重，可执行文件只插入一次

	// PATH 切分成目录数组
	static std::vector<std::string> split_PATH(const std::string &path_list)
	{
		std::vector<std::string> out;
		size_t start = 0;
		while (true)
		{
			size_t end = path_list.find(kPathDelim, start);
			std::string dir = (end == std::string::npos) ? path_list.substr(start) : path_list.substr(start, end - start);
			if (dir.empty())
				dir = ".";
			out.push_back(dir);
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return out;
	}

	// 读取目录最后修改时间
	static std::optional<fs::file_time_type> get_dir_last_write_time(const std::string &dir)
	{
		std::error_code ec;
		if (!fs::exists(dir, ec) or ec)
			return std::nullopt;
		if (!fs::is_directory(dir, ec) or ec)
			return std::nullopt;
		auto t = fs::last_write_time(dir, ec);
		if (ec)
			return std::nullopt;
		return t;
	}

	// 判断是否需要重建， PATH 变了或者 某个目录发生了变化
	bool need_rebuild(const std::string &current_path) const
	{
		if (current_path != cached_path)
			return true;
		auto cur_dirs = split_PATH(current_path);
		if (cur_dirs.size() != dirs.size())
			return true;
		for (size_t i = 0; i < cur_dirs.size(); i++)
		{
			if (cur_dirs[i] != dirs[i])
				return true;
			auto last_time = get_dir_last_write_time(cur_dirs[i]);
			if (last_time.has_value() != dir_last_write_time[i].has_value())
				return true;
			if (last_time.has_value() and dir_last_write_time[i].has_value() and last_time.value() != dir_last_write_time[i].value())
				return true;
		}
		return false;
	}

	// 重建 Trie
	void rebuild(const std::string &current_path)
	{
		cached_path = current_path;
		dirs = split_PATH(current_path);
		dir_last_write_time.clear();
		dir_last_write_time.reserve(dirs.size());

		trie.clear();
		seen.clear();
		for (const auto &s : builtin_commands)
		{
			trie.insert(s);
			seen.insert(s);
		}

		for (const auto &dir : dirs)
		{
			dir_last_write_time.push_back(get_dir_last_write_time(dir));
			std::error_code ec;
			fs::directory_iterator it(dir, ec), endit;
			if (ec)
				continue;
			while (!ec and it != endit)
			{
				fs::path p = it->path();
				std::string name = p.filename().string();
				if (!name.empty() and seen.insert(name).second)
				{
					if (is_executable_file(p))
					trie.insert(name);
					else
						seen.erase(name);
				}
				it.increment(ec);
			}
		}
	}

	void ensure_up_to_date()
	{
		const char *env = std::getenv("PATH");
		std::string current_path = env ? std::string(env) : std::string();
		if (cached_path.empty() or need_rebuild(current_path))
			rebuild(current_path);
	}
};

static AutoCompletionCache g_completion_cache;

static bool g_table_pending = false; // 是否已经按过一次 Tab
static std::string g_pending_prefix; // 第一次 Tab 时输入的前缀
static std::string g_pending_buffer; // 第一次 Tab 时整行缓冲区内容。
static int g_pending_point = 0; // 第一次 Tab 时光标位置
static std::vector<std::string> g_pending_matches; //第一次 Tab 时计算好的候选列表，第二次 Tab 直接打印

// 只有一个匹配时得到完整单词
static std::optional<std::string> get_unique_match (const std::string &prefix) 
{
	auto v = g_completion_cache.trie.list_with_prefix(prefix, 1);
	if (v.size() == 1) return v[0];
	else return std::nullopt;
}

static int tab_handler(int count, int key)
{
	g_completion_cache.ensure_up_to_date();
	std::string buf = rl_line_buffer ? std::string(rl_line_buffer) : std::string();
	int point = rl_point;

	// 暂时只补全指令，在输入其他参数的时候先不管
	size_t first_space = buf.find_first_of(" \t");
	if (first_space != std::string::npos and point > (int)first_space)
	{
		g_table_pending = false;
		return 0;
	}

	std::string prefix = buf.substr(0, (size_t)point);
	int cnt = g_completion_cache.trie.count_with_prefix(prefix);
	if (cnt == 0)
	{
		std::cout<<Bell<<std::flush;
		g_table_pending = false;
		return 0;
	}
	else if (cnt == 1)
	{
		auto res = get_unique_match(prefix);
		if (res.has_value())
		{
			rl_delete_text(0, point);
			rl_point = 0;
			std::string s = *res + " ";
			rl_insert_text(s.c_str());
			rl_point = (int)s.size();
			rl_redisplay();
		}
		g_table_pending = false;
		return 0;
	}
	// 多匹配，先用LCP推进
	std::string lcp = g_completion_cache.trie.LCP_for_prefix(prefix);
	if (lcp.size() > prefix.size())
	{
		rl_delete_text(0, point);
		rl_point = 0;
		rl_insert_text(lcp.c_str()); //仍有多个匹配，不加空格
		rl_point = (int)lcp.size();
		rl_redisplay();
		g_table_pending = false;
		return 0;
	}
	// LCP无法推进，响铃 / 列表
	bool second_tab = g_table_pending and
					  prefix == g_pending_prefix and
					  buf == g_pending_buffer and	
					  point == g_pending_point;
	if (!second_tab)
	{
		std::cout<<Bell<<std::flush;
		g_table_pending = true;
		g_pending_prefix = prefix;
		g_pending_buffer = buf;
		g_pending_point = point;
		g_pending_matches = g_completion_cache.trie.list_with_prefix(prefix);
		return 0;
	}
	//第二次 Tab 打印所有匹配项
	std::cout<<std::endl;
	for (size_t i=0;i<g_pending_matches.size();i++)
	{
		if (i) std::cout << "  ";
    	std::cout << g_pending_matches[i];
	}
	std::cout<<std::endl;
	rl_on_new_line();
	rl_redisplay();
	g_table_pending = false;
	return 0;
}


int main()
{
	// Flush after every std::cout / std:cerr
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;

	rl_bind_key('\t', tab_handler);

	while (true)
	{
		char* raw = readline("$ ");
		if (!raw) break; //EOF
		std::string line(raw);
		std::free(raw);

		g_table_pending = false;
		if (!line.empty()) add_history(line.c_str());
		
		std::vector<Token> tokens = parse_line(line);
		if (tokens.empty())
			continue;

		std::optional<std::string> stdout_path;
		bool stdout_append = false;
		std::optional<std::string> stderr_path;
		bool stderr_append = false;

		extract_redirections(tokens, stdout_path, stdout_append, stderr_path, stderr_append); // 去除重定向相关的，最后只剩arg参数

		std::vector<std::string> args = tokens_to_words(tokens);
		if (args.empty())
			continue;

		std::string command = args[0];

		if (builtin_commands.count(command))
		{
			// 对于内置指令的重定向
			Stdout_Rirect_Guard out_guard;
			Stderr_Rirect_Guard err_guard;
			if (stdout_path.has_value())
			{
				if (!out_guard.redirect_to(*stdout_path, stdout_append))
				{
					std::cerr << "redirection failed: " << *stdout_path << std::endl;
					continue;
				}
			}
			if (stderr_path.has_value())
			{
				if (!err_guard.redirect_to(*stderr_path, stderr_append))
				{
					std::cerr << "redirection failed: " << *stderr_path << std::endl;
					continue;
				}
			}

			if (command == "exit")
				return 0;
			else if (command == "cd")
			{
				if (args.size() < 2)
					continue; // 缺少参数
				std::string &target = args[1];
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
				for (size_t i = 1; i < args.size(); i++)
				{
					if (i > 1)
						std::cout << " ";
					std::cout << args[i];
				}
				std::cout << std::endl;
			}
			else if (command == "type")
			{
				// 无第二个 token
				if (args.size() < 2)
				{
					std::cout << ": not found" << std::endl;
					continue;
				}

				std::string q = args[1];
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
					continue;
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
		}
		else
		{
			const std::string &cmd = args[0];
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

				// 对于子进程处理重定向
				if (stdout_path.has_value())
				{
					int fd = stdout_append ? open_file_append(*stdout_path) : open_file_trunc(*stdout_path);
					if (fd < 0)
						_exit(1);
					if (dup2(fd, STDOUT_FILENO) < 0)
					{
						close(fd);
						_exit(1);
					}
					close(fd);
				}
				if (stderr_path.has_value())
				{
					int fd = stderr_append ? open_file_append(*stderr_path) : open_file_trunc(*stderr_path);
					if (fd < 0)
						_exit(1);
					if (dup2(fd, STDERR_FILENO) < 0)
					{
						close(fd);
						_exit(1);
					}
					close(fd);
				}

				std::vector<char *> argv;
				argv.reserve(args.size() + 1);
				for (auto &t : args)
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
