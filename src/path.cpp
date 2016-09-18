#include "core/system.h"

#include "ai.h"
#include "path.h"
#include "world.h"

#include <cmath>
#include <cassert>

///TEMPORARY
#define SINGLE_ROOM              (0)
#define PATH_STABILITY_DEBUG     (0)
#define PATH_LOG_DETAILED_INFO   (0)
#define PATH_DEBUG_DRAW          (1)
#define PATH_DEBUG_CLOSED_NODES  (0)
#define PATH_DISABLE_VALID_NODES (0)

/*
 * Default constructor, initialise CPathFinder here.
 */

CPathFinder::CPathFinder(room_sector_s* start_sector, room_sector_s* target_sector)
{
    this->m_nodes.clear();
    this->m_openList.clear();
    this->m_closedList.clear();
    this->m_resultPath.clear();
    this->m_flags = 0;
    this->m_startSector = start_sector;
    this->m_targetSector = target_sector;
}

/*
 * Default destructor, uninitialise CPathFinder here.
 */

CPathFinder::~CPathFinder()
{
    if(this->m_nodes.size() > 0)
    {
        for(size_t i = 0; i < this->m_nodes.size(); i++)
        {
            delete this->m_nodes[i];
        }
    }

    this->m_nodes.clear();
    this->m_openList.clear();
    this->m_closedList.clear();
    this->m_resultPath.clear();
    this->m_flags = 0;
    this->m_startSector = NULL;
    this->m_targetSector = NULL;
}

/*
 * This method starts a search from one room sector to the other
 */
void CPathFinder::FindPath(unsigned char flags)///@TODO move params to constructor
{
    //Impossible to continue if either start/target sector pointers are NULL
    assert(this->m_startSector);
    assert(this->m_targetSector);

#if SINGLE_ROOM
    if(this->m_startSector->owner_room != this->m_targetSector->owner_room)
    {
        return;
    }
#endif // SINGLE_ROOM#

    //Set flags so that we can customise the algorithm based on ai types
    this->m_flags |= flags;

    CPathNode* start_node = this->AddNode();
    assert(start_node);
    start_node->SetSector(this->m_startSector);
    this->AddToOpenList(start_node);

    CPathNode* target_node = this->AddNode();
    assert(target_node);
    target_node->SetSector(this->m_targetSector);


    ///Early exit ground/flying entities will never find Lara.
    if(this->m_flags & AIType::GROUND || this->m_flags & AIType::FLYING)
    {
        if(this->m_targetSector->owner_room->flags & TR_ROOM_FLAG_WATER)
        {
            return;///@TODO generate escape path.
        }
    }

    while((this->m_openList.size() > 0))
    {
        if(this->m_nodes.size() > (64*64))///@HACK
        {
             break;
        }

        //Get the next node with lowest cost in the open list
        CPathNode* current_node = this->GetNextOpenNode();
        assert(current_node);

#if PATH_LOG_DETAILED_INFO
        Sys_DebugLog(SYS_LOG_PATHFINDER, "PathFinder is at X: %i, Y: %i\n", current_node->GetSector()->index_x, current_node->GetSector()->index_y);
        Sys_DebugLog(SYS_LOG_PATHFINDER, "Target is at X: %i, Y: %i\n", target->index_x, target->index_y);
#endif // PATH_LOG_DETAILED_INFO

        //If current_node's sector is the target sector we stop!
        if(current_node->GetSector() == this->m_targetSector)
        {
            //We're ready to generate the final path
            this->GeneratePath(current_node);
            break;
        }

        //Remove the current node from the openList and add it to the closed list.
        this->RemoveFromOpenList(current_node);
        this->AddToClosedList(current_node);
///1.
///SEARCHING OVERLAPS
/// |-----------------------| (Y)
/// |   0   |   1   |   2   |  |
/// |-----------------------|  V
/// |   3   | START |   4   |
/// |-----------------------|
/// |   5   |   6   |   7   |
/// |-----------------------|
/// (X) ->
///2.
///Diagonal sectors (0, 2, 5, 7) must cost us more.

        //Iterate through neighbours of the current node, get the one with the lowest F cost
        for(short x = -1; x < 2; x++)
        {
            for(short y = -1; y < 2; y++)
            {
                for(short z = -1; z < 2; z++)///@FIXME - Performance concerns here. Should first check 0
                {
                    //This is the current node, we'll skip it as it's useless!
                    if(x == 0 && y == 0 && z == 0)
                    {
                        continue;
                    }

                    //Grab the neighbour node
                    CPathNode* neighbour_node = this->GetNeighbourNode(x, y, z, current_node);
                    if(neighbour_node == NULL)
                    {
#if PATH_STABILITY_DEBUG
                        Sys_DebugLog(SYS_LOG_PATHFINDER, "[CPathFinder] - Neighbour is NULL!\n");
#endif
                        continue;
                    }

                    //We need to check if this is a valid sector the entity is able to walk upon
                    if(!this->IsValidNeighbour(current_node, neighbour_node))
                    {
                        ///Early out, impossible to reach target
                        if(neighbour_node->GetSector() == target_node->GetSector())
                        {
                            break;
                        }
                        continue;
                    }

                    if(neighbour_node != NULL)
                    {
                        //Get distance between our current_node and the neighbour_node
                        int step_cost = current_node->GetG() + this->GetMovementCost(current_node, neighbour_node);
                        if (step_cost < neighbour_node->GetG())
                        {
                            if (this->IsInOpenList(neighbour_node))
                            {
                                this->RemoveFromOpenList(neighbour_node);
                            }

                            if (this->IsInClosedList(neighbour_node))
                            {
                                this->RemoveFromClosedList(neighbour_node);
                            }
                        }

                        //This is a better route, add it to the open list so we may search it next!
                        if (!this->IsInOpenList(neighbour_node) && !this->IsInClosedList(neighbour_node))
                        {
                            neighbour_node->SetG(step_cost);
                            neighbour_node->SetH(this->CalculateHeuristic(neighbour_node, target_node));
                            neighbour_node->SetParentNode(current_node);
                            this->AddToOpenList(neighbour_node);
                        }
                    }
                }
            }
        }
    }
}

/*
 * Returns node in Open List with lowest F Cost.
 */

CPathNode* CPathFinder::GetNextOpenNode()
{
#if 1
    if(this->m_openList.size() > 0)
    {
        int current_cost = this->m_openList[0]->GetFCost();
        int current_index = 0;

        for(size_t i = 1; i < this->m_openList.size(); i++)
        {
            unsigned int next_cost = this->m_openList.at(i)->GetFCost();
            if(next_cost < current_cost)
            {
                current_cost = next_cost;
                current_index = i;
            }
        }

        return this->m_openList.at(current_index);
    }
    else
    {
        return NULL;
    }
#else
    if(this->m_openList.size() > 0)
    {
        return this->m_openList.at(0);
    }
    else
    {
        return NULL;
    }
#endif
}

/*
 * Adds new node to open list.
 */

void CPathFinder::AddToOpenList(CPathNode* node)
{
    assert(node);
#if 1
    this->m_openList.push_back(node);
#else
    if(this->m_openList.size() > 0)
    {
        unsigned int current_cost = this->m_openList[0]->GetFCost();
        unsigned int current_index = 0;

        if(node->GetFCost() <= current_cost)
        {
            this->m_openList.push_back(node);
            return;
        }

        for(size_t i = 1; i < this->m_openList.size(); i++)
        {
            unsigned int next_cost = this->m_openList.at(i)->GetFCost();
            if(next_cost >= current_cost)
            {
                this->m_openList.insert(this->m_openList.begin() + i, node);
                return;
            }
        }
    }
    else
    {
        this->m_openList.push_back(node);
    }
#endif // 1
}

/*
 * Adds new node to closed list.
 */

void CPathFinder::AddToClosedList(CPathNode* node)
{
    assert(node);
    this->m_closedList.push_back(node);
}

/*
 * Removes specific node pointer from open list.
 */

void CPathFinder::RemoveFromOpenList(CPathNode* node)
{
    assert(node);
    for(size_t i = 0; i < this->m_openList.size(); i++)
    {
        //If we located a pointer match it's the same node
        if(this->m_openList[i] == node)
        {
            this->m_openList.erase(this->m_openList.begin() + i);
            return;
        }
    }
}

/*
 * Removes specific node pointer from closed list.
 */

void CPathFinder::RemoveFromClosedList(CPathNode* node)
{
    assert(node);
    for(size_t i = 0; i < this->m_closedList.size(); i++)
    {
        //If we located a pointer match it's the same node
        if(this->m_closedList[i] == node)
        {
            this->m_closedList.erase(this->m_closedList.begin() + i);
            return;
        }
    }
}

/*
 * Returns 1 if input node is in the open list
 */

bool CPathFinder::IsInOpenList(CPathNode* node)
{
    if(this->m_openList.size() > 0)
    {
        for(size_t i = 0; i < this->m_openList.size(); i++)
        {
            if(this->m_openList[i]->GetSector() == node->GetSector()) return true;
        }
    }

    return false;
}

/*
 * Returns 1 if input node is in the closed list
 */

bool CPathFinder::IsInClosedList(CPathNode* node)
{
    if(this->m_closedList.size() > 0)
    {
        for(size_t i = 0; i < this->m_closedList.size(); i++)
        {
            if(this->m_closedList[i]->GetSector() == node->GetSector()) return true;
        }
    }

    return false;
}

/*
 * Returns CPathNode* at x, y in node list.
 */

CPathNode* CPathFinder::GetNeighbourNode(short x, short y, short z, CPathNode* current_node)///@OPTIMISE if current node is same as target, we don't want to search portals.
{
    assert(current_node);

    room_sector_s* current_sector = current_node->GetSector();
    assert(current_sector);

    room_s* current_room = current_sector->owner_room;
    assert(current_room);

    short neighbour_x = (x + current_sector->index_x);
    short neighbour_y = (y + current_sector->index_y);

    if(z == -1)
    {
        room_sector_s* sector_below = current_sector->sector_below;
        if(sector_below)
        {
            current_room = sector_below->owner_room;
            neighbour_x = (x + sector_below->index_x);
            neighbour_y = (y + sector_below->index_y);
        }
        else
        {
            return NULL;
        }
    }
    else if(z == 1)
    {
        room_sector_s* sector_above = current_sector->sector_above;
        if(sector_above)
        {
            current_room = sector_above->owner_room;
            neighbour_x = (x + sector_above->index_x);
            neighbour_y = (y + sector_above->index_y);
        }
        else
        {
            return NULL;
        }
    }

    //We don't want to process any out of bound sectors (this should never happen)
    if((neighbour_x >= 0) &&
        (neighbour_y >= 0) &&
        (neighbour_x < current_room->sectors_x) &&
        (neighbour_y < current_room->sectors_y))
    {
        //Generate a neighbour node with the newly found sector.
        room_sector_s* neighbour_sector = World_GetRoomSector(current_room->id, neighbour_x, neighbour_y);

        //Disable diagonal
        if(neighbour_sector->index_x != current_sector->index_x && neighbour_sector->index_y != current_sector->index_y && z == 0)
        {
            return NULL;
        }

        CPathNode* neighbour_node = this->AddNode();
        neighbour_node->SetSector(neighbour_sector);
        return neighbour_node;
    }
    else
    {
#if !SINGLE_ROOM

        ///We're only required to search portals if target is NOT in the current room
        if(this->m_targetSector->owner_room == current_node->GetSector()->owner_room)
        {
            return NULL;
        }

        ///An out of bound room sector means we've possibly reached a portal sector (hence why the current neighbour is not in bounds of the current room)
        ///Here we check this and add the sector through the portal so the search is continued.
        room_s* neighbour_room = current_node->GetSector()->portal_to_room;

        if(neighbour_room != NULL)
        {
            ///Here we iterate through all sectors in the neighbour room to find the sector that joins with the current portal
            ///@OPTIMISE We can use World_GetSector... if we know the x,y indexs, should be calculated!
            for(unsigned int i = 0; i < neighbour_room->sectors_count; i++)
            {
                room_sector_s* sector = &neighbour_room->sectors[i];
                if(sector->portal_to_room != current_room)
                {
                    continue;
                }
                else
                {
                     ///@FIXME should check to see which is the best portal sector that our entity can go to!
                    CPathNode* neighbour_node = this->AddNode();
                    neighbour_node->SetSector(sector);

                    if(IsValidNeighbour(current_node, neighbour_node))
                    {
                        return neighbour_node;
                    }
                }
            }
        }
#endif
    }

    return NULL;
}

/*
 * Calculates heuristic distance between two nodes.
 */

int CPathFinder::CalculateHeuristic(CPathNode* start, CPathNode* target)
{
    assert(start);
    assert(target);

    room_sector_s* start_sector = start->GetSector();
    room_sector_s* target_sector = target->GetSector();

    assert(start_sector);
    assert(target_sector);

    int dx = std::abs((start_sector->pos[0] - target_sector->pos[0]));
    int dy = std::abs(start_sector->pos[1] - target_sector->pos[1]);

    if(dx > dy) return 14 * dy + 10 * (dx - dy);
    return (14 * dx) + (10 * (dy - dx));
}

/*
 * Prints final path
 */

void CPathFinder::GeneratePath(CPathNode* end_node)
{
#if PATH_DEBUG_DRAW
    renderer.debugDrawer->SetColor(0.0, 1.0, 0.0);
#endif
    while(end_node->GetParentNode() != NULL)///@FIXME should be end_node?
    {
#if PATH_DEBUG_DRAW
        if(end_node->GetSector() != NULL)
        {
            renderer.debugDrawer->DrawSectorDebugLines(end_node->GetSector());
        }
#endif
        this->m_resultPath.push_back(end_node);
        end_node = end_node->GetParentNode();
    }

#if PATH_DEBUG_CLOSED_NODES
    renderer.debugDrawer->SetColor(1.0, 0.0, 0.0);
    for(size_t i = 0; i < this->m_closedList.size(); i++)
    {
        room_sector_s* sector = this->m_closedList.at(i)->GetSector();
        if(sector != NULL)
        {
            renderer.debugDrawer->DrawSectorDebugLines(sector);
        }
    }

#endif // PATH_DEBUG_CLOSED_NODES
}

/*
 * Returns 1 if entity can move to this sector
 */
///@TODO - This should check sector heights and anything that would block a specific entity type from moving to a sector/node i.e (water, walls, next step too high..)
///@TODO - Ceiling checks, water checks.
bool CPathFinder::IsValidNeighbour(CPathNode* current_node, CPathNode* neighbour_node)
{
    assert(current_node);
    assert(neighbour_node);

    room_sector_s* current_sector = current_node->GetSector();
    room_sector_s* neighbour_sector = neighbour_node->GetSector();

    assert(current_sector);
    assert(neighbour_sector);

    ///Invalid entity cannot move here
    if(neighbour_sector->floor == neighbour_sector->ceiling)
    {
        return false;
    }

    ///Water entities can only move through rooms that have water flag set
    if(this->m_flags & AIType::WATER)
    {
        if(!(neighbour_sector->owner_room->flags & TR_ROOM_FLAG_WATER))
        {
            return false;
        }
    } ///Ground and flying entities should never enter water.
    else if((this->m_flags & AIType::FLYING) || (this->m_flags & AIType::GROUND))
    {
        if(neighbour_sector->owner_room->flags & TR_ROOM_FLAG_WATER)
        {
            return false;
        }
    }

    ///Ground entities can only move on floor, so here we check if the neighbour has a sector below it!
    if((this->m_flags & AIType::GROUND))
    {
        ///This checks floor heights so ground entities don't walk on certain slopes.
        if(current_sector->floor != neighbour_sector->floor)///@TODO Check entity OBB, can entity actually move through this space?
        {
            //Height difference
            ///@TODO revert me!
            int current_sector_num_steps = (current_sector->floor / 256);///@FIXME Illegal height check
            int neighbour_sector_num_steps = (neighbour_sector->floor / 256);///@FIXME Illegal height check
            int step = current_sector_num_steps - neighbour_sector_num_steps;
            //If the current node's floor+1step is higher
            if(step > 1 || step < -1)
            {
                return false;
            }
        }

        room_sector_s* neighbour_sector_below = neighbour_sector->sector_below;
        if(neighbour_sector_below != NULL)
        {
            return false;
        }
    }

    return true;
}

/*
 * Returns movement cost from node A to node B.
 */

int CPathFinder::GetMovementCost(CPathNode* from_node, CPathNode* to_node)
{
    int movement_cost = 0;

#if 0
    CPathNode* parent_node = from_node->GetParentNode();
    if(parent_node != NULL)
    {
        movement_cost += parent_node->GetG();
    }
#endif

    if(from_node->GetSector()->index_x != to_node->GetSector()->index_x && from_node->GetSector()->index_y != to_node->GetSector()->index_y)
    {
        movement_cost += 14;
    }
    else
    {
        movement_cost += 10;
    }

    return movement_cost;
}

/*
 * Adds then returns pointer to new node
 */

CPathNode* CPathFinder::AddNode()
{
    this->m_nodes.emplace_back(new CPathNode());
    CPathNode* node = this->m_nodes[this->m_nodes.size()-1];
}

std::vector<CPathNode*> CPathFinder::GetResultPath()
{
    return this->m_resultPath;
}
