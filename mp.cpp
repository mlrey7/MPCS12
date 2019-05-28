#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "includes.h"
#include "socketstream/socketstream.hh"

using namespace std;
using namespace swoope;

enum class Commands {
  InputPlayerType = 'a',
  InputTeamNumber = 'b',
  Standby = 'c',
  Acknowledged = 'd',
  InputPlayerAction = 'e',
  GameOver = 'f',
  PlayerSkippedStatus = 'g',
  PlayerNotSkippedStatus = 'h',
  TeamSkippedStatus = 'i',
  TeamNotSkippedStatus = 'j'
};

char commandToChar(Commands c) { return static_cast<char>(c); }

Commands charToCommand(char c) { return static_cast<Commands>(c); }

class IServer {
 public:
  virtual void playerHasSkipped(int playerIndex) = 0;
  virtual void onFinishSkipping() = 0;
  virtual void onTeamFinishSkipping() = 0;
  virtual void broadcastStatus(string status) = 0;
  virtual string getPlayerAction(int playerIndex) = 0;
  virtual void teamHasSkipped(int teamIndex) = 0;
  virtual void declareWinner(int winner) = 0;
};

class OnPlayerSkipDelegate {
  IServer* server;

 public:
  OnPlayerSkipDelegate(IServer* server) { this->server = server; }
  void onPlayerSkip(int playerNumber) {
    server->playerHasSkipped(playerNumber);
  }
  void onFinishSkipping() { server->onFinishSkipping(); }
};

class Team {
 private:
  int team_num;
  int n_players;
  int curr_index;
  vector<Player*> players;
  bool dead = false;
  OnPlayerSkipDelegate* onPlayerSkipDelegate;

 public:
  Team(int n);
  int getTeamNumber() { return team_num; }
  bool isDead() { return dead; }
  int get_size() { return n_players; }

  void attachDelegate(OnPlayerSkipDelegate* onPlayerSkipDelegate) {
    this->onPlayerSkipDelegate = onPlayerSkipDelegate;
  }
  void add_player(Player* p) {
    n_players++;
    players.push_back(p);
  }

  Player* getPlayer(int i) { return players[i]; }
  void checkDead() {
    for (int i = 0; i < players.size(); i++) {
      if (!players[i]->isDead()) {
        dead = false;
        return;
      }
    }
    dead = true;
  }

  Player* getCurrentPlayer() {
    int orig_index = curr_index;
    int index = curr_index;
    do {
      if (!players[index]->isDead()) {
        if (!players[index]->turn_skip()) {
          onPlayerSkipDelegate->onFinishSkipping();
          curr_index = (index + 1) % n_players;
          return players[index];
        } else {
          onPlayerSkipDelegate->onPlayerSkip(players[index]->get_ord());
        }
      }
      index = (index + 1) % n_players;
    } while (index != orig_index);
    debug_print("Null");
    // cout << "NullPlayer" << endl;
    // Team has no ready players
    return nullptr;
  }

  string getPrintableStatus() {
    stringstream ss;
    ss << "Team " << team_num << ": ";
    for (int i = 0; i < players.size(); i++) {
      // D: '>>' Signifies team's next player.
      if (i == curr_index) ss << ">>";
      ss << players[i]->getPrintableStatus();
      if (i != players.size() - 1) ss << " | ";
    }
    return ss.str();
  }
};

Team::Team(int n) {
  team_num = n;
  n_players = 0;
  curr_index = 0;
}

class Game {
 private:
  int n_players;
  int n_teams;
  int curr_index;
  vector<Team*> teams;
  vector<Player*> players;
  Player* currentPlayer;
  IServer* server;

 public:
  Game(IServer* server, vector<int> teamNumbers, vector<string> playerTypes) {
    this->server = server;
    n_teams = unordered_set<int>(teamNumbers.begin(), teamNumbers.end()).size();
    for (int i = 1; i <= n_teams; i++) {
      Team* t = new Team(i);
      t->attachDelegate(new OnPlayerSkipDelegate(server));
      teams.push_back(t);
    }
    n_players = playerTypes.size();
    for (int i = 0; i < n_players; i++) {
      Player* p = create_player(playerTypes[i], teamNumbers[i], i + 1);
      teams[teamNumbers[i] - 1]->add_player(p);
      players.push_back(p);
    }
  }

  Player* create_player(string type, int team, int num) {
    Player* p;
    if (type == "human")
      p = new Human(team, num);
    else if (type == "alien")
      p = new Alien(team, num);
    else if (type == "doggo")
      p = new Doggo(team, num);
    else if (type == "zombie")
      p = new Zombie(team, num);
    return p;
  }

  string status() {
    stringstream ss;
    for (int t = 0; t < n_teams; t++) {
      ss << teams[t]->getPrintableStatus() << "\n";
    }
    // D: Displays the current player.
    ss << "\nPlayer " << currentPlayer->get_ord();
    ss << "of Team " << currentPlayer->getTeamNumber() << "\n";
    return ss.str();
  }

  bool OneLeft() {
    int counter = 0;
    for (int i = 0; i < teams.size(); i++) {
      if (!teams[i]->isDead()) counter++;
    }
    return counter == 1;
  }

  void updateTeamStatus() {
    for (int i = 0; i < teams.size(); i++) teams[i]->checkDead();
  }

  void declareWinner() {
    // status();
    server->broadcastStatus(status());
    int winner = 0;
    for (int i = 0; i < teams.size(); i++) {
      if (!teams[i]->isDead()) {
        winner = i + 1;
        cout << "Team " << i + 1 << " wins!" << endl;
      }
    }
    server->declareWinner(winner);
  }

  Player* getPlayer() {
    while (true) {
      if (!teams[curr_index]->isDead()) {
        Player* p = teams[curr_index]->getCurrentPlayer();
        if (p != nullptr) {
          server->onTeamFinishSkipping();
          curr_index = (curr_index + 1) % n_teams;
          currentPlayer = p;
          return p;
        } else {
          // Naskip yung team
          server->teamHasSkipped(curr_index);
        }
      }
      curr_index = (curr_index + 1) % n_teams;
    }
    // cout << "NullTeam" << endl;
    return nullptr;
  }

  void playerTurn(Player* p) {
    for (int i = 0; i < p->getTurns(); i++) {
      string act = server->getPlayerAction(p->get_ord());
      action(p, act);
      if (OneLeft()) {
        declareWinner();
        return;
      }
    }
    // cout << status();
    server->broadcastStatus(status());
  }

  void Start() {
    // cout << status();
    server->broadcastStatus(status());
    while (!OneLeft()) {
      Player* p = getPlayer();
      playerTurn(p);
    }
  }
  bool verifyAction(int playerIndex, string action) {
    stringstream ss(action);
    string command;
    ss >> command;
    Player* currPlayer = players[playerIndex];
    if (command == "tap") {
      int tgt_player;
      string limb, tgt_limb;
      ss >> limb >> tgt_player >> tgt_limb;
      Player* target = players[tgt_player - 1];
      if (target->getTeamNumber() == currPlayer->getTeamNumber()) return false;
      if (target->isDead()) return false;
      Limb* attack_limb = currPlayer->limb_ptr(limb);
      Limb* target_limb = target->limb_ptr(tgt_limb);
      if (attack_limb == nullptr || attack_limb->isDead()) return false;
      if (target_limb == nullptr || target_limb->isDead()) return false;
    } else if (command == "disthands") {
      int allHands = currPlayer->get_hands();
      int liveHands = currPlayer->getNumberOfLivingLimbs(LimbType::HAND);
      int maxDigit = currPlayer->getHand(0)->getMaxDigits();
      if (liveHands == 1) return false;

      vector<int> params;
      int x, pos = 0, total1 = 0;
      while (ss >> x) {
        if (x == maxDigit) return false;
        params.push_back(x);
        total1 += x;
      }
      int size = params.size();
      if (size > liveHands) return false;

      int total2 = 0, same = 0;
      for (int i = 0; (i < allHands) && (pos < size); i++) {
        Limb* hand = currPlayer->getHand(i);
        if (!hand->isDead()) {
          int dgt = hand->get_digits();
          total2 += dgt;
          if (params[pos++] == dgt) same++;
        }
      }
      if (same == liveHands) return false;
      if (total1 != total2) return false;
    } else {
      return false;
    }
    return true;
  }
  void action(Player* p, string action) {
    stringstream ss(action);
    string command;
    ss >> command;
    if (command == "tap") {
      int tgt_player;
      string limb, tgt_limb;
      ss >> limb >> tgt_player >> tgt_limb;
      attack(p, limb, tgt_player, tgt_limb);
    } else if (command == "disthands") {
      vector<int> params;
      int x;
      while (ss >> x) {
        params.push_back(x);
      }
      p->distribute(params, LimbType::HAND);
    }
    updateTeamStatus();
  }

  // D: Possible series.
  string possibleActions(int playerIdx) {
    Player* actor = players[playerIdx];
    stringstream main_menu;
    main_menu << possibleTaps(actor);
    main_menu << possibleDistH(actor) << "\n";
    return main_menu.str();
  }
  string possibleTaps(Player* actor) {
    int myTeam = actor->getTeamNumber();
    stringstream menu;
    menu << "Possible Taps:\n";
    for (int i = 1; i <= n_teams; i++) {
      if (i != myTeam) {
        menu << "> Team " << i << ":\n";
        int size = teams[i]->get_size();
        for (int j = 0; j < size; j++) {
          Player* p = teams[i]->getPlayer(j);
          menu << " > Player " << p->get_ord() << "\n";

          menu << "     >> Hands:";
          int h = p->get_hands();
          for (int k = 0; k < h; k++) {
            menu << " " << (k + 1) << ":" << p->getHand(k)->get_digits();
          }

          menu << "\n    >> Feet: ";
          int f = p->get_feet();
          for (int k = 0; k < f; k++) {
            menu << " " << (k + 1) << ":" << p->getFoot(k)->get_digits();
          }
        }
        menu << "\n";
      }
    }
    return menu.str();
  }
  string possibleDistH(Player* p) {
    stringstream menu;
    menu << "Living Hands:";
    int h = p->get_hands();
    for (int i = 0; i < h; i++) {
      Limb* hand = p->getHand(i);
      if (!hand->isDead())
        menu << " " << hand->get_digits();
      else
        menu << " X";
    }
    menu << "\n";
    return menu.str();
  }
  void attack(Player* atk_player, string atk_limb, int tgt_p, string tgt_limb) {
    Player* tgt_player = players[tgt_p - 1];
    tgt_player->attacked(tgt_limb, atk_player->getLimbDigits(atk_limb));
    atk_player->notify(EventType::hasAttacked);
    if (tgt_player->get_type() == PlayerType::dog)
      atk_player->notify(EventType::hasAttackedDog);
  }
};

class Common {
 protected:
  int n;
  string port;
  int playerNumber = 0;
  int teamNumber;
  string playerType;
  Player* player;

 public:
  int getTeamNumber() { return teamNumber; }
  bool validPlayerType(string a) {
    if (a == "human" || a == "alien" || a == "zombie" || a == "doggo")
      return true;
    return false;
  }
  void inputPlayerType() {
    cout << "Ready" << endl;
    while (!cin.good() || !validPlayerType(playerType)) {
      cout << "Pick a player type: " << endl;
      cout << "human" << endl
           << "alien" << endl
           << "doggo" << endl
           << "zombie" << endl;
      getline(cin, playerType);
    }
  }
  void inputTeamNumber() {
    while (!cin.good() || teamNumber < 1 || teamNumber > n) {
      cout << "Input your team number from 1-" << n << endl;
      cin >> teamNumber;
    }
  }
};

class Server : public Common, public IServer {
 private:
  socketstream* clients;
  socketstream listener;
  Game* game;
  map<int, bool> playerSkipState;
  map<int, bool> teamSkipState;

 public:
  Server(string port, int n) {
    this->port = port;
    this->n = n;
  }
  bool validTeamNumber(vector<int> teamNumbers) {
    int last_team = 0;
    int s = teamNumbers.size();
    int* emptyCheck = new int(s + 1);
    fill_n(emptyCheck, s + 1, 0);
    for (int i = 0; i < s; i++) {
      int team = teamNumbers[i];
      emptyCheck[team]++;
      if (last_team < team) last_team = team;
    }
    for (int i = 1; i <= last_team; i++) {
      if (emptyCheck[i] == 0) return false;
    }
    return true;
  }
  void initialize() {
    vector<string> playerTypes;
    vector<int> teamNumbers;

    listener.open(port, n - 1);
    cout << "Opening in port " << port << endl;
    cout << n << " players" << endl;
    cout << "Waiting for connection...\n";
    clients = new socketstream[n];
    for (int i = 1; i < n; i++) {
      cout << "Waiting for P" << (i + 1) << "...\n";
      listener.accept(clients[i]);
      clients[i] << i << endl;
      cout << "P" << (i + 1) << " ready.\n";
    }
    for (int i = 1; i < n; i++) {
      clients[i] << 1 << endl;
    }
    inputPlayerType();
    playerTypes.push_back(playerType);
    for (int i = 1; i < n; i++) {
      clients[i] << commandToChar(Commands::InputPlayerType) << endl;
      string s;
      getline(clients[i], s);
      playerTypes.push_back(s);
    }
    while (!validTeamNumber(teamNumbers)) {
      inputTeamNumber();
      teamNumbers.push_back(this->teamNumber);
      for (int i = 1; i < n; i++) {
        clients[i] << commandToChar(Commands::InputTeamNumber) << endl;
        int t;
        clients[i] >> t;
        clients[i].ignore();
        teamNumbers.push_back(t);
      }
    }
    for (int i = 1; i < n; i++) {
      clients[i] << commandToChar(Commands::Acknowledged) << endl;
    }
    for (int i = 0; i < n; i++) {
      teamSkipState.insert(pair<int, bool>(i, false));
      playerSkipState.insert(pair<int, bool>(i, false));
    }

    game = new Game(this, teamNumbers, playerTypes);
    game->Start();
  }
  string getPlayerAction(int playerIndex) {
    string action;
    // D: Menu to be displayed.
    string menu = game->possibleActions(playerIndex);
    if (playerIndex == 0) {
      while (!game->verifyAction(playerIndex, action)) {
        cout << menu;
        getline(cin, action);
      }
    } else {
      while (!game->verifyAction(playerIndex, action)) {
        clients[playerIndex] << commandToChar(Commands::InputPlayerAction)
                             << endl;
        clients[playerIndex] << menu << endl;
        getline(clients[playerIndex], action);
      }
      clients[playerIndex] << commandToChar(Commands::Acknowledged) << endl;
    }
    return action;
  }
  void broadcastStatus(string status) {
    cout << status;
    for (int i = 1; i < n; i++) {
      clients[i] << status << endl;
    }
  }
  void playerHasSkipped(int playerIndex) {
    playerSkipState[playerIndex] = true;
  }
  void teamHasSkipped(int teamIndex) { teamSkipState[teamIndex] = true; }
  void onTeamFinishSkipping() {
    stringstream ss;
    ss << "Team ";
    for (int i = 0; i < n; i++) {
      if (teamSkipState[i]) ss << i + 1;
      if (i != n - 1) ss << ",";
    }
    ss << "has skipped\n";
    if (ss.str() != "Team ") {
      cout << ss.str() << endl;
      for (int i = 1; i < n; i++) {
        clients[i] << commandToChar(Commands::TeamSkippedStatus) << endl;
        clients[i] << ss.str();
      }
    } else {
      for (int i = 1; i < n; i++) {
        clients[i] << commandToChar(Commands::TeamNotSkippedStatus) << endl;
      }
    }
  }
  void onFinishSkipping() {
    stringstream ss;
    ss << "Player ";
    for (int i = 0; i < n; i++) {
      if (playerSkipState[i]) ss << i + 1;
      if (i != n - 1) ss << ",";
    }
    ss << "has skipped\n";
    if (ss.str() != "Player ") {
      cout << ss.str() << endl;
      for (int i = 1; i < n; i++) {
        clients[i] << commandToChar(Commands::PlayerSkippedStatus) << endl;
        clients[i] << ss.str();
      }
    } else {
      for (int i = 1; i < n; i++) {
        clients[i] << commandToChar(Commands::PlayerNotSkippedStatus) << endl;
      }
    }
  }
  void declareWinner(int winner) {
    if (teamNumber == winner) {
      cout << "Congratulations: Team " << winner << " wins!" << endl;
    } else {
      cout << "You Lose; Team " << winner << " wins!" << endl;
    }
    for (int i = 1; i < n; i++) {
      clients[i] << commandToChar(Commands::GameOver) << endl;
      clients[i] << winner << endl;
    }
  }
  void start() { game->Start(); }
};

class Client : public Common {
 private:
  string ip;
  socketstream server;

 public:
  Client(string port, string ip) {
    this->port = port;
    this->ip = ip;
  }
  void initialize() {
    char cmd;
    server.open(ip, port);
    // if (!server.good()) return false;
    server >> playerNumber;
    server.ignore();
    cout << "Your player number is: " << playerNumber + 1 << endl;
    int ready = 0;
    cout << "Waiting for other players" << endl;
    server >> ready;
    server.ignore();
    cout << "Ready" << endl;
    server >> cmd;
    server.ignore();
    if (charToCommand(cmd) == Commands::InputPlayerType) {
      inputPlayerType();
      server << playerType << endl;
    }
    while (charToCommand(cmd) != Commands::Acknowledged) {
      server >> cmd;
      server.ignore();
      if (charToCommand(cmd) == Commands::InputTeamNumber) {
        inputTeamNumber();
        server << playerType << endl;
      }
    }
    while (true) {
      string status;
      getline(server, status);
      cout << status << endl;
      server >> cmd;
      server.ignore();
      if (charToCommand(cmd) == Commands::PlayerSkippedStatus) {
        string skipstats;
        getline(server, skipstats);
        cout << skipstats << endl;
      } else if (charToCommand(cmd) == Commands::PlayerNotSkippedStatus) {
        // Do nothing
      }
      server >> cmd;
      server.ignore();
      if (charToCommand(cmd) == Commands::TeamSkippedStatus) {
        string skipstats;
        getline(server, skipstats);
        cout << skipstats << endl;
      } else if (charToCommand(cmd) == Commands::TeamNotSkippedStatus) {
        // Do nothing
      }
      server >> cmd;
      server.ignore();
      if (charToCommand(cmd) == Commands::InputPlayerAction) {
        while (charToCommand(cmd) == Commands::InputPlayerAction) {
          server >> cmd;
          server.ignore();
          string action, menu;

          // D: receives menu to display.
          getline(server, menu);
          cout << menu << "\n";

          getline(cin, action);
          server << action << endl;
        }
      } else if (charToCommand(cmd) == Commands::Standby) {
        // Do nothing
      } else if (charToCommand(cmd) == Commands::GameOver) {
        int winner;
        server >> winner;
        server.ignore();
        stringstream ss;
        if (teamNumber == winner) {
          cout << "Congratulations: Team " << winner << " wins!\n";
        } else {
          cout << "You Lose; Team " << winner << " wins!\n";
        }
        break;
      }
    }
  }
};

int main(int argc, char* argv[]) {
  if (argc == 1) {
    cout << "Error No args found" << endl;
    return 0;
  }

  int p = atoi(argv[1]);
  if (p < 1024 || p > 65535) {
    cout << "ERROR: Port must be within range of 1024-65535" << endl;
    return 0;
  }
  string port = to_string(p);
  if (argc == 2) {
    int n;
    while (!cin.good() || n < 2 || n > 6) {
      cout << "Enter the number of player between 2-6" << endl;
      cin >> n;
      cin.ignore();
    }
    Server* server = new Server(port, n);
    server->initialize();
  } else if (argc == 3) {
    string ip = argv[2];
    Client* client = new Client(port, ip);
    client->initialize();
    // if (!client->initialize()) {
    //   cout << "ERROR: IP INVALID" << endl;
    //   return 0;
    // }
  }
}
