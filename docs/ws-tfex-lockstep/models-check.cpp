#include "Models.hpp"
#include <cstdio>
#include <cstring>
using namespace tfex;
int main() {
    static json::Node nodes[4096]; json::Doc d{nodes, 4096, 0};
    const char* in = "{\"Name\":\"a\\nb\",\"RoomId\":\"r\",\"Players\":[{\"Name\":\"P\\tQ\",\"Seat\":0,\"IsHost\":true}],\"GameData\":{\"Mode\":3},"
                     "\"Series\":{\"BestOf\":3,\"Sides\":[[0],[1]],\"Status\":\"InProgress\",\"Picks\":[]},\"MatchPlayers\":[]}";
    if (!d.Parse(in, (int)strlen(in))) { puts("parse failed"); return 1; }
    static models::Lobby l; models::Read(d, d.Root(), &l);
    printf("MapId (absent) = %d\nHasSeries %d: %s\n", l.Data.MapId, l.HasSeries, l.Series);
    static char buf[16384]; json::Writer w; w.Init(buf, sizeof buf);
    models::Write(w, nullptr, l);
    printf("%s\n", buf);
    static models::Lobby e; e.Clear(); printf("cleared MapId %d\n", e.Data.MapId);
}
