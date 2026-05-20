// src/TrieIndex.cpp
#include "TrieIndex.h"

TrieIndex::TrieIndex() : m_count(0) {
    m_root = new TrieNode();
}

TrieIndex::~TrieIndex() {
    Clear();
    delete m_root;
}

wchar_t TrieIndex::ToLower(wchar_t ch) {
    if (ch >= L'A' && ch <= L'Z')
        return ch + (L'a' - L'A');
    return ch;
}

void TrieIndex::Insert(const wchar_t* key, int item_id) {
    TrieNode* node = m_root;
    while (*key) {
        wchar_t ch = ToLower(*key);
        auto it = node->children.find(ch);
        if (it == node->children.end()) {
            TrieNode* child = new TrieNode();
            node->children[ch] = child;
            node = child;
        } else {
            node = it->second;
        }
        key++;
    }
    if (!node->is_end) {
        node->is_end = true;
        node->data_id = item_id;
        m_count++;
    }
}

void TrieIndex::CollectSubtree(TrieNode* node, std::vector<int>& results, int maxResults) {
    if (!node) return;
    if (node->is_end) {
        results.push_back(node->data_id);
        if (maxResults > 0 && (int)results.size() >= maxResults)
            return;
    }
    for (auto it = node->children.begin(); it != node->children.end(); ++it) {
        if (maxResults > 0 && (int)results.size() >= maxResults)
            return;
        CollectSubtree(it->second, results, maxResults);
    }
}

void TrieIndex::PrefixSearch(const wchar_t* prefix, std::vector<int>& results, int maxResults) {
    results.clear();
    TrieNode* node = m_root;
    while (*prefix) {
        wchar_t ch = ToLower(*prefix);
        auto it = node->children.find(ch);
        if (it == node->children.end())
            return;
        node = it->second;
        prefix++;
    }
    CollectSubtree(node, results, maxResults);
}

void TrieIndex::Clear() {
    for (auto it = m_root->children.begin(); it != m_root->children.end(); ++it) {
        FreeNode(it->second);
    }
    m_root->children.clear();
    m_count = 0;
}

void TrieIndex::Swap(TrieIndex& other) {
    std::swap(m_root, other.m_root);
    std::swap(m_count, other.m_count);
}

void TrieIndex::FreeNode(TrieNode* node) {
    if (!node) return;
    for (auto it = node->children.begin(); it != node->children.end(); ++it) {
        FreeNode(it->second);
    }
    delete node;
}
