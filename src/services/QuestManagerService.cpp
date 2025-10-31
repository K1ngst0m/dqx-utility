#include "QuestManagerService.hpp"

static QuestManager* g_quest_manager = nullptr;

QuestManager* QuestManagerService_Get()
{
    return g_quest_manager;
}

void QuestManagerService_Set(QuestManager* manager)
{
    g_quest_manager = manager;
}
