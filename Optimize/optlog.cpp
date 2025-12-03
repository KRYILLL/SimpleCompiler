#include <vector>
#include <string>
#include <cstring>
#include "optlog.h"
#include "tac.h"

namespace {
struct Entry {
    OPT_PASS pass;
    int per_pass_index;
    int delta;
    std::vector<std::string> lines;
};

std::vector<Entry> g_entries;
int g_pass_counts[OPT_PASS_COUNT];

const char *pass_name(OPT_PASS pass)
{
    switch(pass)
    {
        case OPT_PASS_CONSTFOLD: return "constant folding";
        case OPT_PASS_COPYPROP:  return "copy propagation";
        default: return "optimization";
    }
}

const char *metric_name(OPT_PASS pass)
{
    switch(pass)
    {
        case OPT_PASS_CONSTFOLD: return "folds";
        case OPT_PASS_COPYPROP:  return "replacements";
        default: return "changes";
    }
}
}

extern "C" void optlog_reset(void)
{
    g_entries.clear();
    std::memset(g_pass_counts, 0, sizeof(g_pass_counts));
}

extern "C" void optlog_record(OPT_PASS pass, const char * const *lines, int line_count, int delta)
{
    Entry entry;
    entry.pass = pass;
    entry.per_pass_index = ++g_pass_counts[pass];
    entry.delta = delta;
    if(lines != nullptr && line_count > 0)
    {
        entry.lines.reserve(static_cast<size_t>(line_count));
        for(int i = 0; i < line_count; ++i)
        {
            if(lines[i] != nullptr)
            {
                entry.lines.emplace_back(lines[i]);
            }
        }
    }
    g_entries.push_back(std::move(entry));
}

extern "C" void optlog_emit(FILE *out)
{
    if(out == nullptr) return;
    if(g_entries.empty()) return;

    int totals[OPT_PASS_COUNT] = {0};

    for(const Entry &entry : g_entries)
    {
        const char *name = pass_name(entry.pass);
        if(entry.per_pass_index > 1)
        {
            out_str(out, "\n\t# %s pass (iteration %d)\n", name, entry.per_pass_index);
        }
        else
        {
            out_str(out, "\n\t# %s pass\n", name);
        }

        if(entry.lines.empty())
        {
            if(entry.delta == 0)
            {
                out_str(out, "\t#   no changes\n");
            }
        }
        else
        {
            for(const std::string &line : entry.lines)
            {
                out_str(out, "\t#   %s\n", line.c_str());
            }
        }

        if(entry.delta > 0)
        {
            out_str(out, "\t#   %s this iteration: %d\n", metric_name(entry.pass), entry.delta);
        }

        totals[entry.pass] += entry.delta;
    }

    // Add a trailing blank line for readability
    out_str(out, "\n");

    for(int pass = 0; pass < OPT_PASS_COUNT; ++pass)
    {
        if(g_pass_counts[pass] == 0) continue;
        const char *name = pass_name(static_cast<OPT_PASS>(pass));
        const char *metric = metric_name(static_cast<OPT_PASS>(pass));
        out_str(out, "\t# %s total %s: %d\n", name, metric, totals[pass]);
    }

    out_str(out, "\n");

    optlog_reset();
}
