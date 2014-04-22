/*
 *   FILE: toplevel.c
 * AUTHOR: name (email)
 *   DATE: March 31 23:59:59 PST 2013
 *  DESCR:
 */

/* #define DEBUG */

#include "main.h"
#include <string>
#include "mazewar.h"

static bool		updateView;	/* true if update needed */
MazewarInstance::Ptr M;

/* Use this socket address to send packets to the multi-cast group. */
static Sockaddr         groupAddr;
#define MAX_OTHER_RATS  (MAX_RATS - 1)

static uint32_t currentMessageId = 0;

// last timestamp when JoinMessage is sent. This is only updated in JOIN_PHASE, once 300ms
double lastJoinMsgSendTime = 0;

// last timestamp when HitMessage is sent. This is only updated in HIT_PHASE, once 200ms
double lastHitMsgSendTime = 0;

// first JoinMessage sent time. JOIN_PHASE lasts 3s
double firstJoinMsgSendTime = 0;

// first HitMessage sent time. HIT_PHASE lasts 2s
double firstHitMsgSendTime = 0;

// last timestamp when KeepAliveMessage is sent. This is sent once 200ms. 
double lastKeepAliveMsgSendTime = 0;

// last timestamp the missile position get updated in manageMissiles
double lastMissilePosUpdateTime = 0;


int main(int argc, char *argv[])
{
    Loc x(1);
    Loc y(5);
    Direction dir(0);
    char *ratName;

    signal(SIGHUP, quit);
    signal(SIGINT, quit);
    signal(SIGTERM, quit);

    if (argc == 5) {
    	ratName = (char*)malloc((unsigned) (strlen(argv[1]) + 1));
    	ratName[strlen(argv[1])] = 0;
    	strncpy(ratName, argv[1], strlen(argv[1]));
    	printf("Welcome to CS244B MazeWar!\n\n");
    } else {
	    getName("Welcome to CS244B MazeWar!\n\nYour Name", &ratName);
	    ratName[strlen(ratName)-1] = 0;
	}

    M = MazewarInstance::mazewarInstanceNew(string(ratName));
    MazewarInstance* a = M.ptr();
    memset(M->myName_, 0, NAMESIZE);
    strncpy(M->myName_, ratName, NAMESIZE);
    free(ratName);

    MazeInit(argc, argv);

    if (argc == 5) {
    	M->xlocIs(atoi(argv[2]));
		M->ylocIs(atoi(argv[3]));
		// match input to direction representation
		if (!strcmp(argv[4], "n"))
			M->dirIs(NORTH);
		else if (!strcmp(argv[4], "s"))
			M->dirIs(SOUTH);
		else if (!strcmp(argv[4], "e"))
			M->dirIs(EAST);
		else if (!strcmp(argv[4], "w"))
			M->dirIs(WEST);
		else
			M->dirIs(NORTH);
	} else {
		NewPosition(M);
    }

    printf("My RatName: %s\n", M->myName_);
	printf("My RatId: ");
	printRatId(M->my_ratId.m_ratId);

    myStatusPrint();

    /* So you can see what a Rat is supposed to look like, we create
    one rat in the single player mode Mazewar.
    It doesn't move, you can't shoot it, you can just walk around it */

    play();

    return 0;
}


/* ----------------------------------------------------------------------- */

void
play(void)
{
	MWEvent		event;

	event.eventSource = groupAddr;

	while (TRUE) {
		NextEvent(&event, M->theSocket());
		if (!M->peeking())
			switch(event.eventType) {
			case EVENT_A:
				aboutFace();
				break;

			case EVENT_S:
				leftTurn();
				break;

			case EVENT_D:
				forward();
				break;

			case EVENT_F:
				rightTurn();
				break;

			case EVENT_BAR:
				backward();
				break;

			case EVENT_LEFT_D:
				peekLeft();
				break;

			case EVENT_MIDDLE_D:
				shoot();
				break;

			case EVENT_RIGHT_D:
				peekRight();
				break;

			case EVENT_NETWORK:
				break;

			case EVENT_INT:
				quit(0);
				break;

			}
		else
			switch (event.eventType) {
			case EVENT_RIGHT_U:
			case EVENT_LEFT_U:
				peekStop();
				break;

			case EVENT_NETWORK:
				break;
			}

		ratStates();		/* clean house */

		manageMissiles();

		DoViewUpdate();

		/* Any info to send over network? */

		// if I am in join phase, send JoinMessage for 3 seconds
		if (M->myCurrPhaseState() == JOIN_PHASE) {
			joinPhase();
		}

		// if I am in play phase, check if I am hit by some other missiles
		if (M->myCurrPhaseState() == PLAY_PHASE) {
			playPhase();
		}

		// if I am in hit phase, send HitMessage until receiving HitResponseMessage
		if (M->myCurrPhaseState() == HIT_PHASE) {
			hitPhase();
		}

		// send keep alive message every 200ms
		if (getCurrentTime() - lastKeepAliveMsgSendTime >= KEEPALIVE_INTERVAL) {
			sendKeepAliveMessage();
			lastKeepAliveMsgSendTime = getCurrentTime();
		}

		// check if there is any other player has not send any KeepAliveMessage for more than 10 seconds
		checkKeepAliveTimeout();

		// remove one entry 5s after receiving HitMessage
		for (map<uint32_t, VictimRat>::iterator it = M->hitVictimMap.begin(); it != M->hitVictimMap.end();) {
			if (getCurrentTime() - it->second.recvHitMessageTimestamp >= VICTIMRAT_STORE_TIMEOUT) {
				M->hitVictimMap.erase(it++);
			} else
				it++;
		}
	}
}

/* ----------------------------------------------------------------------- */

static	Direction	_aboutFace[NDIRECTION] ={SOUTH, NORTH, WEST, EAST};
static	Direction	_leftTurn[NDIRECTION] =	{WEST, EAST, NORTH, SOUTH};
static	Direction	_rightTurn[NDIRECTION] ={EAST, WEST, SOUTH, NORTH};

void
aboutFace(void)
{
	M->dirIs(_aboutFace[MY_DIR]);
	updateView = TRUE;
}

/* ----------------------------------------------------------------------- */

void
leftTurn(void)
{
	M->dirIs(_leftTurn[MY_DIR]);
	updateView = TRUE;
}

/* ----------------------------------------------------------------------- */

void
rightTurn(void)
{
	M->dirIs(_rightTurn[MY_DIR]);
	updateView = TRUE;
}

/* ----------------------------------------------------------------------- */

/* remember ... "North" is to the right ... positive X motion */

void
forward(void)
{
	register int	tx = MY_X_LOC;
	register int	ty = MY_Y_LOC;

	switch(MY_DIR) {
	case NORTH:	if (!M->maze_[tx+1][ty])	tx++; break;
	case SOUTH:	if (!M->maze_[tx-1][ty])	tx--; break;
	case EAST:	if (!M->maze_[tx][ty+1])	ty++; break;
	case WEST:	if (!M->maze_[tx][ty-1])	ty--; break;
	default:
		MWError("bad direction in Forward");
	}
	if ((MY_X_LOC != tx) || (MY_Y_LOC != ty)) {
		if (checkConflict(tx, ty))
			return;

		M->xlocIs(Loc(tx));
		M->ylocIs(Loc(ty));
		updateView = TRUE;
	}
}

/* ----------------------------------------------------------------------- */

void backward()
{
	register int	tx = MY_X_LOC;
	register int	ty = MY_Y_LOC;

	switch(MY_DIR) {
	case NORTH:	if (!M->maze_[tx-1][ty])	tx--; break;
	case SOUTH:	if (!M->maze_[tx+1][ty])	tx++; break;
	case EAST:	if (!M->maze_[tx][ty-1])	ty--; break;
	case WEST:	if (!M->maze_[tx][ty+1])	ty++; break;
	default:
		MWError("bad direction in Backward");
	}
	if ((MY_X_LOC != tx) || (MY_Y_LOC != ty)) {
		if (checkConflict(tx, ty))
			return;

		M->xlocIs(Loc(tx));
		M->ylocIs(Loc(ty));
		updateView = TRUE;
	}
}

/* ----------------------------------------------------------------------- */

void peekLeft()
{
	M->xPeekIs(MY_X_LOC);
	M->yPeekIs(MY_Y_LOC);
	M->dirPeekIs(MY_DIR);

	switch(MY_DIR) {
	case NORTH:	if (!M->maze_[MY_X_LOC+1][MY_Y_LOC]) {
				M->xPeekIs(MY_X_LOC + 1);
				M->dirPeekIs(WEST);
			}
			break;

	case SOUTH:	if (!M->maze_[MY_X_LOC-1][MY_Y_LOC]) {
				M->xPeekIs(MY_X_LOC - 1);
				M->dirPeekIs(EAST);
			}
			break;

	case EAST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC+1]) {
				M->yPeekIs(MY_Y_LOC + 1);
				M->dirPeekIs(NORTH);
			}
			break;

	case WEST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC-1]) {
				M->yPeekIs(MY_Y_LOC - 1);
				M->dirPeekIs(SOUTH);
			}
			break;

	default:
			MWError("bad direction in PeekLeft");
	}

	/* if any change, display the new view without moving! */

	if ((M->xPeek() != MY_X_LOC) || (M->yPeek() != MY_Y_LOC)) {
		M->peekingIs(TRUE);
		updateView = TRUE;
	}
}

/* ----------------------------------------------------------------------- */

void peekRight()
{
	M->xPeekIs(MY_X_LOC);
	M->yPeekIs(MY_Y_LOC);
	M->dirPeekIs(MY_DIR);

	switch(MY_DIR) {
	case NORTH:	if (!M->maze_[MY_X_LOC+1][MY_Y_LOC]) {
				M->xPeekIs(MY_X_LOC + 1);
				M->dirPeekIs(EAST);
			}
			break;

	case SOUTH:	if (!M->maze_[MY_X_LOC-1][MY_Y_LOC]) {
				M->xPeekIs(MY_X_LOC - 1);
				M->dirPeekIs(WEST);
			}
			break;

	case EAST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC+1]) {
				M->yPeekIs(MY_Y_LOC + 1);
				M->dirPeekIs(SOUTH);
			}
			break;

	case WEST:	if (!M->maze_[MY_X_LOC][MY_Y_LOC-1]) {
				M->yPeekIs(MY_Y_LOC - 1);
				M->dirPeekIs(NORTH);
			}
			break;

	default:
			MWError("bad direction in PeekRight");
	}

	/* if any change, display the new view without moving! */

	if ((M->xPeek() != MY_X_LOC) || (M->yPeek() != MY_Y_LOC)) {
		M->peekingIs(TRUE);
		updateView = TRUE;
	}
}

/* ----------------------------------------------------------------------- */

void peekStop()
{
	M->peekingIs(FALSE);
	updateView = TRUE;
}

/* ----------------------------------------------------------------------- */

void shoot()
{
	M->scoreIs( MY_SCORE-1 );
	UpdateScoreCard(M->myRatId().value());

	// I shoot a missile
	M->missileExistIs(true);
	M->missileXLocIs(Loc(MY_X_LOC));
	M->missileYLocIs(Loc(MY_Y_LOC));
	M->missileDirIs(Direction(MY_DIR));

	switch (MY_MISSILE_DIR) {
	case NORTH:	showMissile(MY_MISSILE_X_LOC + 1, MY_MISSILE_Y_LOC, 0, 0, 0, false); break;
	case SOUTH:	showMissile(MY_MISSILE_X_LOC - 1, MY_MISSILE_Y_LOC, 0, 0, 0, false); break;
	case EAST:	showMissile(MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC + 1, 0, 0, 0, false); break;
	case WEST:	showMissile(MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC - 1, 0, 0, 0, false); break;
	}
	updateView = TRUE;
	// must update here
	lastMissilePosUpdateTime = getCurrentTime();
}

/* ----------------------------------------------------------------------- */

/*
 * Exit from game, clean up window
 */

void quit(int sig)
{
	// Send leave message here
	sendLeaveMessage();

	StopWindow();
	exit(0);
}


/* ----------------------------------------------------------------------- */

void NewPosition(MazewarInstance::Ptr m)
{
	Loc newX(0);
	Loc newY(0);
	Direction dir(0); /* start on occupied square */

	bool occupied = false;
	while (M->maze_[newX.value()][newY.value()] || occupied == true) {
	  /* MAZE[XY]MAX is a power of 2 */
	  newX = Loc(random() & (MAZEXMAX - 1));
	  newY = Loc(random() & (MAZEYMAX - 1));

	  /* In real game, also check that square is
	     unoccupied by another rat */  
	  for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
	  	if (newX.value() == it->second.rat.x.value() && newY.value() == it->second.rat.y.value()) {
	  		occupied = true;
	  		break;
	  	}
	  	else
	  		occupied = false;
	  }
	}

	/* prevent a blank wall at first glimpse */

	if (!m->maze_[(newX.value())+1][(newY.value())]) dir = Direction(NORTH);
	if (!m->maze_[(newX.value())-1][(newY.value())]) dir = Direction(SOUTH);
	if (!m->maze_[(newX.value())][(newY.value())+1]) dir = Direction(EAST);
	if (!m->maze_[(newX.value())][(newY.value())-1]) dir = Direction(WEST);

	m->xlocIs(newX);
	m->ylocIs(newY);
	m->dirIs(dir);
}

/* ----------------------------------------------------------------------- */

void MWError(char *s)

{
	StopWindow();
	fprintf(stderr, "CS244BMazeWar: %s\n", s);
	perror("CS244BMazeWar");
	exit(-1);
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
Score GetRatScore(RatIndexType ratId)
{
  if (ratId.value() == 	M->myRatId().value())
    { return(M->score()); }
  else { return (0); }
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
char *GetRatName(RatIndexType ratId)
{
  if (ratId.value() ==	M->myRatId().value())
    { return(M->myName_); }
  else { return "Dummy"; }
}

/* ----------------------------------------------------------------------- */

void ConvertIncoming(Message *p, const char* buf)
{
	// receive packet header
	unsigned char msgType = buf[0];
	unsigned char ratId[UUID_SIZE];
	memset(ratId, 0, UUID_SIZE);
	memcpy(ratId, buf + 2, UUID_SIZE);
	uint32_t msg_msgId = 0; 
	memcpy(&msg_msgId, buf + 2 + UUID_SIZE, 4);
	uint32_t msgId = ntohl(msg_msgId);

	// ignore receving messages that sent by myself
	bool isMsgSentByMe = isRatIdEquals(M->my_ratId.m_ratId, ratId);
	if (isMsgSentByMe)
		return;
	
	/*
		printf("Recive Message Header\n");
		printf("Message type: 0x%x\n", msgType);
		printf("RatId: ");
		printRatId(ratId);
		printf("Message Id: %u\n", msgId);
	}*/

    switch (msgType) {
    	case JOIN:
    	{
	    	unsigned char name_len = buf[HEADER_SIZE];
	    	char name[NAMESIZE];
	    	memset(name, 0, NAMESIZE);
	    	memcpy(name, buf + HEADER_SIZE + 1, (size_t)name_len);
	    	p = new JoinMessage(ratId, msgId, name_len, name);

	    	#ifdef _DEBUG_
	    	recvMsgPrint(p);
	    	#endif

	    	JoinMessage *joinMsg = dynamic_cast<JoinMessage *>(p);
			process_recv_JoinMessage(joinMsg);
    		break;
    	}
    	case JNRS:
    	{
    		// if I am out of Join phase, I do not care any join response message
    		if (M->myCurrPhaseState() != JOIN_PHASE)
    			return;

	    	unsigned char senderId[UUID_SIZE];
	    	memset(senderId, 0, UUID_SIZE);
	    	memcpy(senderId, buf + HEADER_SIZE, UUID_SIZE);
	    	unsigned char name_len = buf[HEADER_SIZE + UUID_SIZE];
	    	char name[NAMESIZE];
	    	memset(name, 0, NAMESIZE);
	    	memcpy(name, buf + HEADER_SIZE + UUID_SIZE + 1, (size_t)name_len);
	    	p = new JoinResponseMessage(ratId, msgId, name_len, name, senderId);

			#ifdef _DEBUG_
	    	recvMsgPrint(p);
	    	#endif

	    	JoinResponseMessage *joinResponseMsg = dynamic_cast<JoinResponseMessage *>(p);
			process_recv_JoinResponseMessage(joinResponseMsg);
    		break;
    	}
    	case KPLV:
    	{
			unsigned char ratPosX = buf[HEADER_SIZE];
			unsigned char ratPosY = buf[HEADER_SIZE + 1];
			unsigned char ratDir = buf[HEADER_SIZE + 2];
			int score = 0;
			uint32_t msg_score = 0;
			memcpy(&msg_score, buf + HEADER_SIZE + 3, 4);
			score = (int)ntohl(msg_score);
			unsigned char missileFlag = buf[HEADER_SIZE + 7];

			// if missileFlag is not zero, set missile info
			if (missileFlag == 0) {
				p = new KeepAliveMessage(ratId, msgId, ratPosX, ratPosY, ratDir, score, missileFlag);	
			} else {
				unsigned char missilePosX = buf[HEADER_SIZE + 8];
				unsigned char missilePosY = buf[HEADER_SIZE + 9];
				uint32_t msg_missileSeqNum = 0;
				memcpy(&msg_missileSeqNum, buf + HEADER_SIZE + 10, 4);
				uint32_t missileSeqNum = ntohl(msg_missileSeqNum);
				p = new KeepAliveMessage(ratId, msgId, ratPosX, ratPosY, ratDir, score, missileFlag, missilePosX, missilePosY, missileSeqNum);
			}
	    	
	    	#ifdef _DEBUG_
	    	recvMsgPrint(p);
	    	#endif

	    	KeepAliveMessage *keepAliveMsg = dynamic_cast<KeepAliveMessage *>(p);
			process_recv_KeepAliveMessage(keepAliveMsg);
	    	break;
    	}
    	case LEAV:
    	{
    		p = new LeaveMessage(ratId, msgId);

    		#ifdef _DEBUG_
    		recvMsgPrint(p);
    		#endif

    		LeaveMessage *leaveMsg = dynamic_cast<LeaveMessage *>(p);
  			process_recv_LeaveMessage(leaveMsg);
    		break;
    	}
    	case HITM:
    	{
    		unsigned char shooterId[UUID_SIZE];
    		memset(shooterId, 0, UUID_SIZE);
    		memcpy(shooterId, buf + HEADER_SIZE, UUID_SIZE);
    		uint32_t msg_missileSeqNum = 0;
    		memcpy(&msg_missileSeqNum, buf + HEADER_SIZE + UUID_SIZE, 4);
    		uint32_t missileSeqNum = ntohl(msg_missileSeqNum);
    		p = new HitMessage(ratId, msgId, shooterId, missileSeqNum);

    		#ifdef DEBUG
    		recvMsgPrint(p);
    		#endif

    		HitMessage *hitMsg = dynamic_cast<HitMessage *>(p);
			process_recv_HitMessage(hitMsg);
    		break;
    	}
    	case HTRS:
    	{
    		// if I am out of hit phase, I do not care any hit response message
    		if (M->myCurrPhaseState() != HIT_PHASE)
    			return;

    		unsigned char victimId[UUID_SIZE];
    		memset(victimId, 0, UUID_SIZE);
    		memcpy(victimId, buf + HEADER_SIZE, UUID_SIZE);
    		uint32_t msg_missileSeqNum = 0;
    		memcpy(&msg_missileSeqNum, buf + HEADER_SIZE + UUID_SIZE, 4);
    		uint32_t missileSeqNum = ntohl(msg_missileSeqNum);
    		p = new HitResponseMessage(ratId, msgId, victimId, missileSeqNum);

    		#ifdef DEBUG
    		recvMsgPrint(p);
    		#endif

			HitResponseMessage *hitResponseMsg = dynamic_cast<HitResponseMessage *>(p);
			process_recv_HitResponseMessage(hitResponseMsg);
    		break;
    	}
    	default:
    		printf("Receive invalid message type\n");
    		break;
    }

    delete p;
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own if necessary */
void ConvertOutgoing(Message *p)
{
}

void sendKeepAliveMessage() {
	KeepAliveMessage keepAliveMsg(M->my_ratId.m_ratId, getMessageId(), 
									(unsigned char)MY_X_LOC, (unsigned char)MY_Y_LOC, (unsigned char)MY_DIR, MY_SCORE, 
									(unsigned char)MY_MISSILE_EXIST, (unsigned char)MY_MISSILE_X_LOC, (unsigned char)MY_MISSILE_Y_LOC, MY_MISSILE_SEQNUM);
	#ifdef _DEBUG_
	sendMsgPrint(&keepAliveMsg);
	#endif

	char msg_buf[HEADER_SIZE + 14];
	memset(msg_buf, 0, HEADER_SIZE + 14);
	msg_buf[0] = keepAliveMsg.msgType;
	memcpy(msg_buf + 2, keepAliveMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(keepAliveMsg.msgId);
	memcpy(msg_buf + 2 + UUID_SIZE, &msg_msgId, 4);
	msg_buf[HEADER_SIZE] = keepAliveMsg.ratPosX;
	msg_buf[HEADER_SIZE + 1] = keepAliveMsg.ratPosY;
	msg_buf[HEADER_SIZE + 2] = keepAliveMsg.ratDir;
	uint32_t msg_score = htonl((uint32_t)keepAliveMsg.score);
	memcpy(msg_buf + HEADER_SIZE + 3, &msg_score, 4);
	msg_buf[HEADER_SIZE + 7] = keepAliveMsg.missileFlag;
	msg_buf[HEADER_SIZE + 8] = keepAliveMsg.missilePosX;
	msg_buf[HEADER_SIZE + 9] = keepAliveMsg.missilePosY;
	uint32_t msg_missileSeqNum = htonl(keepAliveMsg.missileSeqNum);
	memcpy(msg_buf + HEADER_SIZE + 10, &msg_missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 14, 0, 
			(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendLeaveMessage() {
	LeaveMessage leaveMsg(M->my_ratId.m_ratId, getMessageId());
	#ifdef _DEBUG_
	sendMsgPrint(&leaveMsg);
	#endif

	char msg_buf[HEADER_SIZE];
	memset(msg_buf, 0, HEADER_SIZE);
	msg_buf[0] = leaveMsg.msgType;
	memcpy(msg_buf + 2, leaveMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(leaveMsg.msgId);
	memcpy(msg_buf + 2 + UUID_SIZE, &msg_msgId, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendJoinMessage() {
	JoinMessage joinMsg(M->my_ratId.m_ratId, getMessageId(), strlen(M->myName_), M->myName_);
	#ifdef _DEBUG_
	sendMsgPrint(&joinMsg);
	#endif

	char msg_buf[HEADER_SIZE + 21];
	memset(msg_buf, 0, HEADER_SIZE + 21);
	msg_buf[0] = joinMsg.msgType;
	memcpy(&msg_buf[2], joinMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(joinMsg.msgId);
	memcpy(&msg_buf[2 + UUID_SIZE], &msg_msgId, 4);
	msg_buf[HEADER_SIZE] = joinMsg.len;
	memcpy(&msg_buf[HEADER_SIZE + 1], joinMsg.name, (size_t)joinMsg.len);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 21, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendJoinResponseMessage(unsigned char *senderId) {
	JoinResponseMessage joinResponseMsg(M->my_ratId.m_ratId, getMessageId(), strlen(M->myName_), M->myName_, senderId);
	#ifdef _DEBUG_
	sendMsgPrint(&joinResponseMsg);
	#endif

	char msg_buf[HEADER_SIZE + 37];
	memset(msg_buf, 0, HEADER_SIZE + 37);
	memcpy(msg_buf, &joinResponseMsg.msgType, 1);
	memcpy(msg_buf + 2, joinResponseMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(joinResponseMsg.msgId);
	memcpy(msg_buf + 2 + UUID_SIZE, &msg_msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, joinResponseMsg.senderId, UUID_SIZE);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &joinResponseMsg.len, 1);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE + 1, joinResponseMsg.name, (size_t)joinResponseMsg.len);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 37, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendHitMessage(unsigned char *shooterId, uint32_t other_missileSeqNum) {
	HitMessage hitMsg(M->my_ratId.m_ratId, getMessageId(), shooterId, other_missileSeqNum);
	#ifdef DEBUG
	sendMsgPrint(&hitMsg);
	#endif	

	char msg_buf[HEADER_SIZE + 20];
	memset(msg_buf, 0, HEADER_SIZE + 20);
	memcpy(msg_buf, &hitMsg.msgType, 1);
	memcpy(msg_buf + 2, hitMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(hitMsg.msgId);
	memcpy(msg_buf + 2 + UUID_SIZE, &msg_msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, hitMsg.shooterId, UUID_SIZE);
	uint32_t msg_missileSeqNum = htonl(hitMsg.missileSeqNum);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &msg_missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 20, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendHitResponseMessage(unsigned char *victimId, uint32_t missileSeqNum) {
	HitResponseMessage hitResponseMsg(M->my_ratId.m_ratId, getMessageId(), victimId, missileSeqNum);
	#ifdef DEBUG
	sendMsgPrint(&hitResponseMsg);
	#endif

	char msg_buf[HEADER_SIZE + 20];
	memset(msg_buf, 0, HEADER_SIZE + 20);
	memcpy(msg_buf, &hitResponseMsg.msgType, 1);
	memcpy(msg_buf + 2, hitResponseMsg.ratId, UUID_SIZE);
	uint32_t msg_msgId = htonl(hitResponseMsg.msgId);
	memcpy(msg_buf + 2 + UUID_SIZE, &msg_msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, hitResponseMsg.victimId, UUID_SIZE);
	uint32_t msg_missileSeqNum = htonl(hitResponseMsg.missileSeqNum);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &msg_missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 20, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

/* send one message nice print */
void sendMsgPrint(Message *p) {
	for (int i = 0; i < 100; i++)
		printf("*");
	printf("\n");
	printf("Send Message:\n");
	p->print();
	for (int i = 0; i < 100; i++)
		printf("*");
	printf("\n\n");
}
/* recv one message nice print */
void recvMsgPrint(Message *p) {
	for (int i = 0; i < 100; i++)
		printf("+");
	printf("\n");
	printf("Receive Message:\n");
	p->print();
	for (int i = 0; i < 100; i++)
		printf("+");
	printf("\n\n");
}

void printRatId(const unsigned char* ratId) {
	for (int i = 0 ; i < UUID_SIZE; i++) {
		printf("%02x", ratId[i]);
	}
	printf("\n");
}

void printOtherRatsNames() {
	printf("Out of join phase, other rats info: \n");
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		printf("RatName: %s, RatId: ", it->second.ratName);
		printRatId(it->first.m_ratId);
	}
	printf("\n");
}

void printOtherRatsInfo() {
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		printf("Other rat Status: \nRatName: %s, RatId: ", it->second.ratName);
		printRatId(it->first.m_ratId);
		printf("Rat ratPosX: %u, ratPosY: %u, ratDir: %u, score: %d\n", it->second.rat.x.value(), it->second.rat.y.value(), it->second.rat.dir.value(), it->second.score);
		printf("Missile exist: %u, missilePosX: %u, missilePosY: %u, missileSeqNum: %u\n", it->second.missile.exist, it->second.missile.x.value(), it->second.missile.y.value(), it->second.missile.seqNum);
	}
	printf("\n");
}

void myStatusPrint() {
	printf("My Status: \n");
	printf("PosX: %u, PosY: %u, Dir: %u, score: %d\n", MY_X_LOC, MY_Y_LOC, MY_DIR, MY_SCORE);
	printf("My Missile Exist: %d\n", MY_MISSILE_EXIST);
}

void UpdateOtherRatsScoreCard() {
	// draw other rats score to the screen
	int i = 1;
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		if (strlen(it->second.ratName) > 0) {
			UpdateScoreCard(MY_RAT_INDEX + i, it->second.ratName, it->second.score);
			++i;
		}
	}
}

/* One Rat gets a new message Id */
uint32_t getMessageId() {
	return currentMessageId++;
}

double getCurrentTime() {
	struct timeval tv; 
	memset(&tv, 0, sizeof(struct timeval));
	gettimeofday(&tv, NULL);  	

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool isRatIdEquals(const unsigned char* myRatId, const unsigned char* recvRatId) {
	return memcmp(myRatId, recvRatId, UUID_SIZE) == 0;
}

void joinPhase() {
	// send JoinMessage every JOIN_INTERVAL
	if (getCurrentTime() - lastJoinMsgSendTime >= JOIN_INTERVAL) {
		// update first JoinMessage send time only once
		if (firstJoinMsgSendTime == 0)
			firstJoinMsgSendTime = getCurrentTime();

		sendJoinMessage();
		lastJoinMsgSendTime = getCurrentTime();	
	}

	// JOIN_PHASE will last JOIN_PHASE_LASTTIME
	if (firstJoinMsgSendTime != 0 && getCurrentTime() - firstJoinMsgSendTime >= JOIN_PHASE_LASTTIME) {
		M->myCurrPhaseStateIs(PLAY_PHASE);

		printOtherRatsNames();
	}
}

void playPhase() {
	// check if I am hit by a missile
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		if (it->second.missile.exist == true && it->second.missile.x.value() == MY_X_LOC && it->second.missile.y.value() == MY_Y_LOC) {
			// I am hit by a missile, send HitMessage and go to HIT_PHASE
			// I can only be hit by only one missile at one time
			printf("I am hit by a missile with seqNum %u from ratId: ", it->second.missile.seqNum);
			printRatId(it->first.m_ratId);

			memcpy(M->hitMissileShooterId.m_ratId, it->first.m_ratId, UUID_SIZE);
			M->hitMissileSeqNum = it->second.missile.seqNum;
			M->myCurrPhaseStateIs(HIT_PHASE);

			firstHitMsgSendTime	= getCurrentTime();
			sendHitMessage(M->hitMissileShooterId.m_ratId, M->hitMissileSeqNum);
			lastHitMsgSendTime = getCurrentTime();
			return;
		}
	}
}

void hitPhase() {
	if (getCurrentTime() - lastHitMsgSendTime >= HIT_INTERVAL) {
		sendHitMessage(M->hitMissileShooterId.m_ratId, M->hitMissileSeqNum);
		lastHitMsgSendTime = getCurrentTime();
	}

	if (firstHitMsgSendTime != 0 && getCurrentTime() - firstHitMsgSendTime >= HIT_PHASE_LASTTIME) {
		M->myCurrPhaseStateIs(PLAY_PHASE);

		memset(M->hitMissileShooterId.m_ratId, 0, UUID_SIZE);
		M->hitMissileSeqNum = -1;
		firstHitMsgSendTime = 0;
	}
}

bool checkConflict(int tx, int ty) {
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		if (tx == it->second.rat.x.value() && ty == it->second.rat.y.value())
			return true;
	}
	return false;
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
void ratStates()
{
	#ifdef _DEBUG_
	myStatusPrint();
	printOtherRatsInfo();
	#endif

	UpdateOtherRatsScoreCard();
}

/* ----------------------------------------------------------------------- */
// This function is only used to update my missile position if the missile exists
void manageMissiles()
{
	// update my missile info once 200ms
	// TODO: when shoot a missile, must update lastMissilePosUpdateTime
	if (MY_MISSILE_EXIST == true) {
		int step = (getCurrentTime() - lastMissilePosUpdateTime) / MISSILE_UPDATE_INTERVAL;
		for (int i = 0; step > 0 && i <= step; i++) {
			Loc prevMissileXLoc = MY_MISSILE_X_LOC;
			Loc prevMissileYLoc = MY_MISSILE_Y_LOC;
			
			switch(MY_MISSILE_DIR) {
				case NORTH:	M->missileXLocIs(Loc(MY_MISSILE_X_LOC + 1)); break;
				case SOUTH:	M->missileXLocIs(Loc(MY_MISSILE_X_LOC - 1)); break;
				case EAST:	M->missileYLocIs(Loc(MY_MISSILE_Y_LOC + 1)); break;
				case WEST:	M->missileYLocIs(Loc(MY_MISSILE_Y_LOC - 1)); break;
				default:
				{
					M->missileExistIs(false);
					M->missileSeqNumIs(MY_MISSILE_SEQNUM + 1);
					break;
				}
			}

			#ifdef DEBUG
			printf("Manage My Missile Status: \n");
			printf("Exist: %d, X: %u, Y: %u, dir: %u, SeqNum: %d\n\n", MY_MISSILE_EXIST, MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC, MY_MISSILE_DIR, MY_MISSILE_SEQNUM);
			#endif

			sendKeepAliveMessage();
			lastKeepAliveMsgSendTime = getCurrentTime();
			// missile hit the wall
			if (M->maze_[MY_MISSILE_X_LOC][MY_MISSILE_Y_LOC] || MY_MISSILE_EXIST == false) {
				printf("My missile: %u hits the wall. missilePosX: %u, missilePosY: %u\n", MY_MISSILE_SEQNUM, MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC);
				printf("My missile previous PosX: %u, PosY: %u\n", prevMissileXLoc.value(), prevMissileYLoc.value());

				clearSquare(prevMissileXLoc, prevMissileYLoc);
				if (prevMissileXLoc == MY_X_LOC && prevMissileYLoc == MY_Y_LOC)
					ShowPosition(MY_X_LOC, MY_Y_LOC, MY_DIR);
				updateView = TRUE;

				M->missileExistIs(false);
				M->missileXLocIs(0);
				M->missileYLocIs(0);
				M->missileDirIs(0);
				M->missileSeqNumIs(MY_MISSILE_SEQNUM + 1);
				lastMissilePosUpdateTime = getCurrentTime();
				return;
			}
			// show missile if not hit the wall
			showMissile(MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC, 0, prevMissileXLoc, prevMissileYLoc, true);
			updateView = TRUE;
		}
		lastMissilePosUpdateTime = getCurrentTime();
	} 

}

/* ----------------------------------------------------------------------- */

void DoViewUpdate()
{
	if (updateView) {	/* paint the screen */
		ShowPosition(MY_X_LOC, MY_Y_LOC, MY_DIR);
		if (M->peeking())
			ShowView(M->xPeek(), M->yPeek(), M->dirPeek());
		else
			ShowView(MY_X_LOC, MY_Y_LOC, MY_DIR);
		updateView = FALSE;
	}
}

/* ----------------------------------------------------------------------- */

/*
 * Sample code to send a packet to a specific destination
 */

/*
 * Notice the call to ConvertOutgoing.  You might want to call ConvertOutgoing
 * before any call to sendto.
 */

void sendPacketToPlayer(RatId ratId, Message *msg)
{
/*
	MW244BPacket pack;
	DataStructureX *packX;

	pack.type = PACKET_TYPE_X;
	packX = (DataStructureX *) &pack.body;
	packX->foo = d1;
	packX->bar = d2;

        ....

	ConvertOutgoing(pack);

	if (sendto((int)mySocket, &pack, sizeof(pack), 0,
		   (Sockaddr) destSocket, sizeof(Sockaddr)) < 0)
	  { MWError("Sample error") };
*/
}


void process_recv_JoinMessage(JoinMessage *p) {
	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.find(p->ratId);
	if (it != M->otherRatInfoMap.end()) {
		// if find JoinMessage ratId in my otherRatInfo table
		// update the player's ratName if ratName changes
		if (strcmp(it->second.ratName, p->name)) {
			memset(it->second.ratName, 0, NAMESIZE);
			memcpy(it->second.ratName, p->name, (size_t)p->len);

			printf("Receive JoinMessage and update ratName: %s, RatId: ", it->second.ratName);
			printRatId(it->first.m_ratId);
		}
	} else {
		MW_RatId other_ratId(p->ratId);
		OtherRat other;
		memset(other.ratName, 0, NAMESIZE);
		memcpy(other.ratName, p->name, (size_t)p->len);
		other.score = 0;
		other.lastKeepAliveRecvTime = getCurrentTime();
		M->otherRatInfoMap.insert(std::make_pair(other_ratId, other));
		
		printf("Receive JoinMessage and store ratName: %s, RatId: ", other.ratName);
		printRatId(other_ratId.m_ratId);
		updateView = TRUE;
	}

	sendJoinResponseMessage(p->ratId);
}

void process_recv_JoinResponseMessage(JoinResponseMessage *p) {
	// Receiving JoinResponseMessage is valid only in JOIN_PHASE and JoinResponseMessage is intended for me 
	if (M->myCurrPhaseState() == JOIN_PHASE && isRatIdEquals(p->senderId, M->my_ratId.m_ratId) == true) {
		map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.find(p->ratId);
		if (it != M->otherRatInfoMap.end()) {
			// if find JoinReponseMessage ratId in my otherRatInfo table
			// update the player's ratName if ratName changes
			if (strcmp(it->second.ratName, p->name)) {
				memset(it->second.ratName, 0, NAMESIZE);
				memcpy(it->second.ratName, p->name, (size_t)p->len);

				printf("Receive JoinResponseMessage and update ratName: %s, RatId: ", it->second.ratName);
				printRatId(it->first.m_ratId);
			}
		} else {
			MW_RatId other_ratId(p->ratId);
			OtherRat other;
			memset(other.ratName, 0, NAMESIZE);
			memcpy(other.ratName, p->name, (size_t)p->len);
			other.score = 0;
			other.lastKeepAliveRecvTime = getCurrentTime();
			M->otherRatInfoMap.insert(std::make_pair(other_ratId, other));
			
			printf("Receive JoinResponseMessage and store ratName: %s, RatId: ", other.ratName);
			printRatId(other_ratId.m_ratId);
		}
	}	
}

void process_recv_KeepAliveMessage(KeepAliveMessage *p) {
	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.find(p->ratId);
	if (it != M->otherRatInfoMap.end()) {
		// find send KeepAliveMessage ratId in my otherRatInfo table
		// update other Rat info in my table
		OtherRat *other = &it->second;
		if (other->rat.playing == false) {
			other->idx = M->myCurrOtherRatIdx();
			if (other->idx.value() == MAX_RATS) 
				MWError("Cannot have more player, reach maximum");

			M->myCurrOtherRatIdxIs(M->myCurrOtherRatIdx().value() + 1);
		}

		if (other->rat.x.value() == p->ratPosX && other->rat.y.value() == p->ratPosY && other->rat.dir.value() == p->ratDir)
			updateView = FALSE;
		else
			updateView = TRUE;

		other->rat.playing = true;
		other->rat.x = Loc(p->ratPosX);
		other->rat.y = Loc(p->ratPosY);
		other->rat.dir = Direction(p->ratDir);
		other->missile.exist = p->missileFlag;
		other->missile.x = Loc(p->missilePosX);
		other->missile.y = Loc(p->missilePosY);
		other->missile.seqNum = p->missileSeqNum;
		other->score = p->score;
		other->lastKeepAliveRecvTime = getCurrentTime();
		M->ratIs(other->rat, other->idx);

		// two rats cannot be at the same position. Check position and resolve conflict if needed
		checkAndResolveRatPosConflict(other->rat.x.value(), other->rat.y.value(), p->ratId);
	} else {
		MW_RatId other_ratId(p->ratId);
		OtherRat other;
		if (other.rat.playing == false) {
			other.idx = M->myCurrOtherRatIdx();
			if (other.idx.value() == MAX_RATS)
				MWError("Cannot have more player, reach maximum");

			M->myCurrOtherRatIdxIs(M->myCurrOtherRatIdx().value() + 1);
		}

		other.rat.playing = true;
		other.rat.x = Loc(p->ratPosX);
		other.rat.y = Loc(p->ratPosY);
		other.rat.dir = Direction(p->ratDir);
		other.missile.exist = p->missileFlag;
		other.missile.x = Loc(p->missilePosX);
		other.missile.y = Loc(p->missilePosY);
		other.missile.seqNum = p->missileSeqNum;
		other.score = p->score;
		other.lastKeepAliveRecvTime = getCurrentTime();
		M->ratIs(other.rat, other.idx);
		M->otherRatInfoMap.insert(std::make_pair(other_ratId, other));

		// two rats cannot be at the same position. Check position and resolve conflict if needed
		checkAndResolveRatPosConflict(other.rat.x.value(), other.rat.y.value(), p->ratId);
		updateView = TRUE;
	}
}

void checkAndResolveRatPosConflict(int otherRatPosX, int otherRatPosY, unsigned char* other_ratId) {
	if (MY_X_LOC == otherRatPosX && MY_Y_LOC == otherRatPosY) {
		// there is position conflict, move rat with smaller ratId
		printf("Two rats at the same position\n");
		if (memcmp(M->my_ratId.m_ratId, other_ratId, UUID_SIZE) < 0) {	
			printf("position conflict happens, resolve conflict\n");
			std::list<Node> l;
			Node my_node(MY_X_LOC, MY_Y_LOC);
			getAdjcentNode(&l, my_node);
			printf("l size: %d\n", l.size());
			
			while(l.size() > 0) {
				Node node = l.front();
				l.pop_front();
				if (isValidPosition(node.x, node.y)) {
					M->xlocIs(Loc(node.x));
					M->ylocIs(Loc(node.y));
					resolveRatPosConflictPrint();
					updateView = TRUE;
					return;
				}
				getAdjcentNode(&l, node);
			}
		}
	}
}

bool isValidPosition(int tx, int ty) {
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end();) {
		if (it->second.rat.x == tx && it->second.rat.y == ty)
			return false;
	}
	return M->maze_[tx][ty] == false;
}

void getAdjcentNode(std::list<Node> *list, Node node) {
	if (node.x + 1 < MAZEXMAX) {
		Node right(node.x + 1, node.y);
		list->push_back(right);
	}
	if (node.x - 1 >= 0) {
		Node left(node.x - 1, node.y);
		list->push_back(left);
	}
	if (node.y + 1 < MAZEYMAX) {
		Node down(node.x, node.y + 1);
		list->push_back(down);
	}
	if (node.y - 1 >= 0) {
		Node up(node.x, node.y - 1);
		list->push_back(up);
	}
}

void resolveRatPosConflictPrint() {
	printf("Rat Position Conflict. New Rat PosX: %u, PosY: %u, RatId: ", MY_X_LOC, MY_Y_LOC);
	printRatId(M->my_ratId.m_ratId);
	printf("\n");
}

void process_recv_LeaveMessage(LeaveMessage *p) {
	// clear all other rats scores in the maze
	int i = 1;
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end(); ++it) {
		if (strlen(it->second.ratName) > 0) {
			ClearScoreLine(MY_RAT_INDEX + i);
			++i;
		}
	}

	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.find(p->ratId);
	if (it != M->otherRatInfoMap.end()) {
		// find sent LeaveMessage ratId in my otherRatInfo table
		// remove this rat info from my table
		printf("Receive LeaveMessage, remove rat with ratId: ");
		printRatId(it->first.m_ratId);

		// need to clear myCurrOtherRatIdx, so next other rat can reuse this
		Rat leftRat = M->rat(it->second.idx.value());	
		leftRat.playing = FALSE;
		leftRat.x = 1;
		leftRat.y = 1;
		leftRat.dir = NORTH;
		M->ratIs(leftRat, it->second.idx.value());

		for (int i = it->second.idx.value(); i < MAX_RATS - 1; i++) {
			Rat rat = M->rat(i);
			Rat copy_rat = M->rat(i + 1);
			rat.playing = copy_rat.playing;
			rat.x = copy_rat.x;
			rat.y = copy_rat.y;
			rat.dir = copy_rat.dir;
			M->ratIs(rat, i);
		}

		M->otherRatInfoMap.erase(it);
		updateView = TRUE;
		// printf("After remove otherRatInfoMap size: %d\n", (uint32_t)M->otherRatInfoMap.size());
		M->myCurrOtherRatIdxIs(M->myCurrOtherRatIdx().value() - 1);	
	}
}

void checkKeepAliveTimeout() {
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfoMap.begin(); it != M->otherRatInfoMap.end();) {
		if((getCurrentTime() - it->second.lastKeepAliveRecvTime) >= KEEPALIVE_TIMEOUT) {
			// clear all other rats scores in the maze
			int j = 1;
			for (map<MW_RatId, OtherRat>::iterator iter = M->otherRatInfoMap.begin(); iter != M->otherRatInfoMap.end(); ++iter) {
				if (strlen(iter->second.ratName) > 0) {
					ClearScoreLine(MY_RAT_INDEX + j);
					++j;
				}
			}

			printf("No KeepAliveMessage Received for more than 10 seconds.\nRemove ratId: ");
			printRatId(it->first.m_ratId);

			// need to clear myCurrOtherRatIdx, so next other rat can reuse this	
			Rat leftRat = M->rat(it->second.idx.value());	
			leftRat.playing = FALSE;
			leftRat.x = 1;
			leftRat.y = 1;
			leftRat.dir = NORTH;
			M->ratIs(leftRat, it->second.idx.value());

			for (int i = it->second.idx.value(); i < MAX_RATS - 1; i++) {
				Rat rat = M->rat(i);
				Rat copy_rat = M->rat(i + 1);
				rat.playing = copy_rat.playing;
				rat.x = copy_rat.x;
				rat.y = copy_rat.y;
				rat.dir = copy_rat.dir;
				M->ratIs(rat, i);
			}

			updateView = TRUE;
			M->myCurrOtherRatIdxIs(M->myCurrOtherRatIdx().value() - 1);	
			M->otherRatInfoMap.erase(it++);
		} else
			it++;	
	}
}

void process_recv_HitMessage(HitMessage *p) {
	if (isRatIdEquals(p->shooterId, M->my_ratId.m_ratId)) {
		map<uint32_t, VictimRat>::iterator it = M->hitVictimMap.find(p->missileSeqNum);

		// only accept the first rat who claims a hit
		// missile sequence number does not exist
		if (it == M->hitVictimMap.end()) {
			printf("Receive HitMessage with seqNum %u from ratId: ", p->missileSeqNum);
			printRatId(p->ratId);

			M->scoreIs( MY_SCORE + 11 );
			UpdateScoreCard(M->myRatId().value());
			// store my missile with seqNum hit someone in the table
			VictimRat victimRat;
			memset(victimRat.victimId.m_ratId, 0, UUID_SIZE);
			memcpy(victimRat.victimId.m_ratId, p->ratId, UUID_SIZE);
			victimRat.recvHitMessageTimestamp = getCurrentTime();

			M->hitVictimMap.insert(std::make_pair(p->missileSeqNum, victimRat));
			sendHitResponseMessage(p->ratId, p->missileSeqNum);

			// clear my missile image and data if not hit the wall (still exist)
			if (MY_MISSILE_EXIST == TRUE) {
				clearSquare(MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC);
				if (MY_MISSILE_X_LOC == MY_X_LOC && MY_MISSILE_Y_LOC == MY_Y_LOC)
					ShowPosition(MY_X_LOC, MY_Y_LOC, MY_DIR);

				M->missileExistIs(FALSE);
				M->missileXLocIs(0);
				M->missileYLocIs(0);
				M->missileDirIs(0);
				M->missileSeqNumIs(MY_MISSILE_SEQNUM + 1);
			}

			updateView = TRUE;
			sendKeepAliveMessage();
		} else {		
			// missile sequence number exists and maps to the same player Id, needs to send HitResponseMessage	
			if (isRatIdEquals(p->ratId, it->second.victimId.m_ratId)) {
				sendHitResponseMessage(p->ratId, p->missileSeqNum);
			} else{	// missile sequence number exists but maps to a different player Id
				// ignore this HitMessage
			}
		}
	}
}

void process_recv_HitResponseMessage(HitResponseMessage *p) {
	if (isRatIdEquals(p->victimId, M->my_ratId.m_ratId)) {
		printf("Receive HitResponseMessage with seqNum %u from ratId: ", p->missileSeqNum);
		printRatId(p->ratId);

		M->scoreIs( MY_SCORE - 5 );
		UpdateScoreCard(M->myRatId().value());
		// regenerate a position for me, and send KeepAliveMessage
		NewPosition(M);
		updateView = TRUE;

		M->myCurrPhaseStateIs(PLAY_PHASE);
		memset(M->hitMissileShooterId.m_ratId, 0, UUID_SIZE);
		M->hitMissileSeqNum = -1;
		firstHitMsgSendTime = 0;

		sendKeepAliveMessage();
	}
}

/* ----------------------------------------------------------------------- */

/* This will presumably be modified by you.
   It is here to provide an example of how to open a UDP port.
   You might choose to use a different strategy
 */
void
netInit()
{
	Sockaddr		nullAddr;
	Sockaddr		*thisHost;
	char			buf[128];
	int				reuse;
	u_char          ttl;
	struct ip_mreq  mreq;

	/* MAZEPORT will be assigned by the TA to each team */
	M->mazePortIs(htons(MAZEPORT));

	gethostname(buf, sizeof(buf));
	if ((thisHost = resolveHost(buf)) == (Sockaddr *) NULL)
	  MWError("who am I?");
	bcopy((caddr_t) thisHost, (caddr_t) (M->myAddr()), sizeof(Sockaddr));

	M->theSocketIs(socket(AF_INET, SOCK_DGRAM, 0));
	if (M->theSocket() < 0)
	  MWError("can't get socket");

	/* SO_REUSEADDR allows more than one binding to the same
	   socket - you cannot have more than one player on one
	   machine without this */
	reuse = 1;
	if (setsockopt(M->theSocket(), SOL_SOCKET, SO_REUSEADDR, &reuse,
		   sizeof(reuse)) < 0) {
		MWError("setsockopt failed (SO_REUSEADDR)");
	}

	nullAddr.sin_family = AF_INET;
	nullAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	nullAddr.sin_port = M->mazePort();
	if (bind(M->theSocket(), (struct sockaddr *)&nullAddr,
		 sizeof(nullAddr)) < 0)
	  MWError("netInit binding");

	/* Multicast TTL:
	   0 restricted to the same host
	   1 restricted to the same subnet
	   32 restricted to the same site
	   64 restricted to the same region
	   128 restricted to the same continent
	   255 unrestricted

	   DO NOT use a value > 32. If possible, use a value of 1 when
	   testing.
	*/

	ttl = 1;
	if (setsockopt(M->theSocket(), IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
		   sizeof(ttl)) < 0) {
		MWError("setsockopt failed (IP_MULTICAST_TTL)");
	}

	/* join the multicast group */
	mreq.imr_multiaddr.s_addr = htonl(MAZEGROUP);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(M->theSocket(), IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)
		   &mreq, sizeof(mreq)) < 0) {
		MWError("setsockopt failed (IP_ADD_MEMBERSHIP)");
	}

	/*
	 * Now we can try to find a game to join; if none, start one.
	 */
	 
	printf("\n");

	/* set up some stuff strictly for this local sample */
	M->myRatIdIs(0);
	M->scoreIs(0);
	SetMyRatIndexType(0);

	/* Get the multi-cast address ready to use in SendData()
           calls. */
	memcpy(&groupAddr, &nullAddr, sizeof(Sockaddr));
	groupAddr.sin_addr.s_addr = htonl(MAZEGROUP);

}


/* ----------------------------------------------------------------------- */
