// TrieIndex.h - Prefix tree (Trie) for auto-completion indexing
#ifndef _TRIE_INDEX_H_
#define _TRIE_INDEX_H_

#include <unordered_map>
#include <vector>
#include <wchar.h>

struct TrieNode {
    bool                                    is_end;
    int                                     data_id;
    std::unordered_map<wchar_t, TrieNode*>  children;

    TrieNode() : is_end(false), data_id(-1) {}
};

class TrieIndex {
public:
    TrieIndex();
    ~TrieIndex();

    void Insert(const wchar_t* key, int item_id);

    void PrefixSearch(const wchar_t* prefix, std::vector<int>& results, int maxResults = 0);

    void Clear();

    void Swap(TrieIndex& other);

    int GetCount() const { return m_count; }

private:
    TrieNode*   m_root;
    int         m_count;

    void CollectSubtree(TrieNode* node, std::vector<int>& results, int maxResults);

    void FreeNode(TrieNode* node);

    static wchar_t ToLower(wchar_t ch);
};

#endif // _TRIE_INDEX_H_
