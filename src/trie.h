#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class Trie
{
private:
    struct Node
    {
        bool is_end = false;
        int subtree_words = 0;
        std::vector<std::pair<unsigned char, int>> child;
    };
    std::vector<Node> nodes;

    // 在 node 的 child 里寻找 ch ,不存则则返回 -1
    static int find_child_index(const Node &node, unsigned char ch)
    {
        auto it = std::lower_bound(node.child.begin(), node.child.end(), ch,
                                   [](const auto &pr, unsigned ch)
                                   { return pr.first < ch; });
        if (it != node.child.end() and it->first == ch)
            return it->second;
        else
            return -1;
    }

    // 在 node 的 child 里寻找 ch ,不存则则创建
    int next_index(int x, unsigned char ch)
    {
        Node &node = nodes[x];
        auto it = std::lower_bound(node.child.begin(), node.child.end(), ch,
                                   [](const auto &pr, unsigned ch)
                                   { return pr.first < ch; });
        if (it != node.child.end() and it->first == ch)
            return it->second;

        int pos = it - node.child.begin();
        int tot = static_cast<int>(nodes.size());
        nodes.push_back(Node());
        nodes[x].child.insert(nodes[x].child.begin() + pos, {ch, tot});
        return tot;
    }

    // dfs 收集所有匹配
    void dfs_collect(int x, std::string &cur, std::vector<std::string> &out, size_t limit) const
    {
        if (out.size() >= limit)
            return;
        const Node &node = nodes[x];

        if (node.is_end)
        {
            out.push_back(cur);
            if (out.size() >= limit)
                return;
        }

        for (const auto &[ch, v] : node.child)
        {
            cur.push_back(ch);
            dfs_collect(v, cur, out, limit);
            cur.pop_back();
            if (out.size() >= limit)
                return;
        }
    }

public:
    Trie() { nodes.push_back(Node()); }

    void clear()
    {
        nodes.clear();
        nodes.push_back(Node());
    }

    bool insert(const std::string &s)
    {
        int x = 0;
        std::vector<int> path;
        path.push_back(x);
        for (auto ch : s)
        {
            x = next_index(x, ch);
            path.push_back(x);
        }
        if (nodes[x].is_end)
            return false;
        nodes[x].is_end = true;
        for (int x : path)
            nodes[x].subtree_words++;
        return true;
    }

    // 找 prefix 对应节点 index ,不存在则返回 -1
    int find_node(const std::string &prefix) const
    {
        int x = 0;
        for (auto ch : prefix)
        {
            int v = find_child_index(nodes[x], ch);
            if (v < 0)
                return -1;
            x = v;
        }
        return x;
    }

    // prefix 下有多少完整单词
    int count_with_prefix(const std::string &prefix) const
    {
        int x = find_node(prefix);
        return x < 0 ? 0 : nodes[x].subtree_words;
    }

    // prefix 可以推进到的最长公共前缀
    std::string LCP_for_prefix(const std::string &prefix) const
    {
        int x = find_node(prefix);
        if (x < 0)
            return prefix;
        std::string res = prefix;

        while (!nodes[x].is_end and nodes[x].child.size() == 1)
        {
            auto [ch, v] = nodes[x].child[0];
            res.push_back(ch);
            x = v;
        }
        return res;
    }

    // 字典序列出 prefix 下的所有匹配， 可选 limit 限制数量
    std::vector<std::string> list_with_prefix(const std::string &prefix, size_t limit = (size_t)-1) const
    {
        std::vector<std::string> out;
        int x = find_node(prefix);
        if (x < 0)
            return out;

        std::string cur = prefix;
        dfs_collect(x, cur, out, limit);
        return out;
    }
};