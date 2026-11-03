/* Edytor grup wirtualnych – logika kroków bez LVGL (spec: virtual-groups, docs/SPEC.md §6.7). */
#include "group_editor.h"

#include <stdio.h>
#include <string.h>

void ed_begin_new(group_editor_t *ed)
{
    memset(ed, 0, sizeof *ed);
    ed->active = true;
    ed->group = -1;
    ed->step = ED_STEP_TYPE;
}

void ed_begin_edit(group_editor_t *ed, const groups_t *gs, int gi, const dev_info_t *devs, size_t n)
{
    memset(ed, 0, sizeof *ed);
    if (gi < 0 || gi >= gs->count)
        return;
    const group_cfg_t *g = &gs->g[gi];
    ed->active = true;
    ed->group = gi;
    ed->group_id = g->id;
    ed->step = ED_STEP_MEMBERS;
    ed->type_chosen = true;
    ed->type = g->type;
    ed->name_edited = true;
    snprintf(ed->name, sizeof ed->name, "%s", g->name);
    ed->position = g->position;
    for (unsigned m = 0; m < g->member_count && m < GROUP_MEMBERS_MAX; m++) {
        int d = groups_find_device(devs, n, g->members[m]);
        if (d >= 0 && d < ED_MAX_DEVICES)
            ed->selected |= 1ull << d;
        else
            ed->missing++;
    }
}

bool ed_select_type(group_editor_t *ed, dev_type_t type)
{
    if (!ed->active || ed->group >= 0 || ed->step != ED_STEP_TYPE)
        return false;
    if (!ed->type_chosen || ed->type != type)
        ed->selected = 0;
    ed->type = type;
    ed->type_chosen = true;
    return true;
}

size_t ed_candidates(const group_editor_t *ed, const dev_info_t *devs, size_t n, int *out, size_t cap)
{
    size_t k = 0;
    if (!ed->type_chosen)
        return 0;
    for (size_t i = 0; i < n && i < ED_MAX_DEVICES && k < cap; i++)
        if (devs[i].type == ed->type)
            out[k++] = (int)i;
    return k;
}

unsigned ed_member_count(const group_editor_t *ed)
{
    unsigned c = 0;
    for (uint64_t s = ed->selected; s != 0; s &= s - 1)
        c++;
    return c;
}

void ed_toggle_member(group_editor_t *ed, const dev_info_t *devs, size_t n, size_t idx)
{
    if (idx >= n || idx >= ED_MAX_DEVICES || !ed->type_chosen || devs[idx].type != ed->type)
        return;
    uint64_t bit = 1ull << idx;
    if (ed->selected & bit)
        ed->selected &= ~bit;
    else if (ed_member_count(ed) < GROUP_MEMBERS_MAX)
        ed->selected |= bit;
}

bool ed_can_next(const group_editor_t *ed)
{
    if (!ed->active)
        return false;
    switch (ed->step) {
    case ED_STEP_TYPE: return ed->type_chosen;
    case ED_STEP_MEMBERS: {
        unsigned c = ed_member_count(ed);
        return c >= GROUP_MEMBERS_MIN && c <= GROUP_MEMBERS_MAX;
    }
    case ED_STEP_NAME: return ed->name[0] != '\0';
    case ED_STEP_POSITION:
    default: return true;
    }
}

bool ed_next(group_editor_t *ed)
{
    if (ed->step == ED_STEP_POSITION || !ed_can_next(ed))
        return false;
    ed->step = (ed_step_t)(ed->step + 1);
    if (ed->step == ED_STEP_NAME && !ed->name_edited)
        ed_default_name(ed->name, sizeof ed->name, ed->type, ed_member_count(ed));
    return true;
}

void ed_back(group_editor_t *ed)
{
    ed_step_t first = ed->group >= 0 ? ED_STEP_MEMBERS : ED_STEP_TYPE;
    if (ed->step <= first)
        ed->active = false;
    else
        ed->step = (ed_step_t)(ed->step - 1);
}

int ed_default_name(char *buf, size_t cap, dev_type_t type, unsigned count)
{
    const char *base = type == DEV_TYPE_LIGHT ? "Światła" : type == DEV_TYPE_PLUG ? "Gniazdka" : "Rolety";
    return snprintf(buf, cap, "%s (%u)", base, count);
}

bool ed_name_append(group_editor_t *ed, const char *utf8_char)
{
    size_t len = strlen(ed->name), add = strlen(utf8_char);
    if (add == 0 || add > 4 || len + add > GROUP_NAME_LEN - 1)
        return false;
    memcpy(ed->name + len, utf8_char, add + 1);
    ed->name_edited = true;
    return true;
}

void ed_name_backspace(group_editor_t *ed)
{
    size_t len = strlen(ed->name);
    while (len > 0) {
        len--;
        if (((unsigned char)ed->name[len] & 0xC0u) != 0x80u) /* początek znaku UTF-8 */
            break;
    }
    ed->name[len] = '\0';
    ed->name_edited = true;
}

bool ed_save(group_editor_t *ed, groups_t *gs, const dev_info_t *devs, size_t n)
{
    if (!ed->active || ed->step != ED_STEP_POSITION || !ed->type_chosen || ed->name[0] == '\0')
        return false;
    unsigned count = ed_member_count(ed);
    if (count < GROUP_MEMBERS_MIN || count > GROUP_MEMBERS_MAX)
        return false;

    group_cfg_t g = { 0 };
    if (ed->group >= 0) {
        if (ed->group >= gs->count)
            return false;
        g.id = gs->g[ed->group].id;
    } else {
        if (!groups_can_add(gs) || (g.id = groups_free_id(gs)) == 0)
            return false;
    }
    snprintf(g.name, sizeof g.name, "%s", ed->name);
    g.type = ed->type;
    g.position = (uint8_t)(ed->position > 255 ? 255 : ed->position);
    for (size_t i = 0; i < n && i < ED_MAX_DEVICES; i++)
        if ((ed->selected & (1ull << i)) && devs[i].type == ed->type && g.member_count < GROUP_MEMBERS_MAX)
            snprintf(g.members[g.member_count++], GROUP_DEVID_LEN, "%s", devs[i].id);

    if (ed->group >= 0)
        gs->g[ed->group] = g;
    else
        gs->g[gs->count++] = g;
    ed->active = false;
    return true;
}

bool ed_delete(group_editor_t *ed, groups_t *gs)
{
    if (!ed->active || ed->group < 0 || ed->group >= gs->count)
        return false;
    if (!groups_delete(gs, gs->g[ed->group].id))
        return false;
    ed->active = false;
    return true;
}
