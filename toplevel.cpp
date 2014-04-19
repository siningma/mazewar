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

static unsigned int currentMessageId = 0;
static unsigned int currentMissileId = 0;

// last timestamp when JoinMessage is sent. This is only updated in JOIN_PHASE, once 300ms
double lastJoinMsgSendTime = 0;

// last timestamp when HitMessage is sent. This is only updated in HIT_PHASE, once 200ms
double lastHitMsgSendTime = 0;

// first JoinMessage sent time. JOIN_PHASE lasts 3 s
double firstJoinMsgSendTime = 0;

// last timestamp when KeepAliveMessage is sent. This is sent once 200ms. 
double lastKeepAliveMsgSendTime = 0;

// last timestamp the missile position get updated in manageMissile()
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

    getName("Welcome to CS244B MazeWar!\n\nYour Name", &ratName);
    ratName[strlen(ratName)-1] = 0;

    M = MazewarInstance::mazewarInstanceNew(string(ratName));
    MazewarInstance* a = M.ptr();
    strncpy(M->myName_, ratName, NAMESIZE);
    free(ratName);	

    printf("RatName size: %u, %d\n", (unsigned int)sizeof(M->myName_), strlen(M->myName_));
    printf("My RatName: %s\n", M->myName_);
	printf("My RatId: ");
	printRatId(M->my_ratId.value());

    myMissileStatusPrint();

    MazeInit(argc, argv);

    NewPosition(M);

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
	Message	*incoming;

	event.eventDetail = incoming;

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
				processPacket(&event);
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
				processPacket(&event);
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
		if (getCurrentTime() - lastKeepAliveMsgSendTime >= KEEPALIVE_INTERVAL * 10) {
			sendKeepAliveMessage();
			lastKeepAliveMsgSendTime = getCurrentTime();
		}

		// check if there is any other player has not send any KeepAliveMessage for more than 10 seconds
		for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.begin(); it != M->otherRatInfo_map.end();) {
			if((getCurrentTime() - it->second.lastKeepAliveRecvTime) >= KEEPALIVE_TIMEOUT) {
				printf("No KeepAliveMessage Received for more than 10 seconds.\nRemove ratId: ");
				printRatId(it->first.m_ratId);
				M->otherRatInfo_map.erase(it++);
			}
			else
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
	M->scoreIs( M->score().value()-1 );
	UpdateScoreCard(M->myRatId().value());
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

	while (M->maze_[newX.value()][newY.value()]) {
	  /* MAZE[XY]MAX is a power of 2 */
	  newX = Loc(random() & (MAZEXMAX - 1));
	  newY = Loc(random() & (MAZEYMAX - 1));

	  /* In real game, also check that square is
	     unoccupied by another rat */
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
  else { return ("Dummy"); }
}

/* ----------------------------------------------------------------------- */

int recvPacket(int socket, unsigned char* payload_buf, int len, struct sockaddr *src_addr, socklen_t *addrlen) {
	int cc;
	int	ret;
	fd_set	fdmask;
	FD_ZERO(&fdmask);
	FD_SET(socket, &fdmask);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	while ((ret = select(socket + 1, &fdmask, NULL, NULL, &timeout)) == -1)
		if (errno != EINTR)
	  		MWError("select error on events");

	if(FD_ISSET(socket, &fdmask))	{
		cc = recvfrom(socket, payload_buf, len, 0,
		        src_addr, addrlen);
		printf("receive packet payload_buf size: %d\n", cc);
		if (cc <= 0) {
		    if (cc < 0 && errno != EINTR) 
				perror("event recvfrom");
		}	
	}
	return cc;
}

/* This is just for the sample version, rewrite your own if necessary */
void ConvertIncoming(Message *p, int socket, const unsigned char* header_buf, struct sockaddr *src_addr, socklen_t *addrlen)
{
	unsigned char msgType = header_buf[0];
	unsigned char ratId[UUID_SIZE];
	memset(ratId, 0, UUID_SIZE);
	memcpy(ratId, header_buf + 2, UUID_SIZE);
	unsigned int msgId = 0; 
	memcpy(&msgId, header_buf + 2 + UUID_SIZE, 4);

	// ignore receving messages that sent by myself
	bool isMsgSentByMe = isRatIdEquals(M->my_ratId.value(), ratId);

    switch (msgType) {
    	case JOIN:
    	{
    		unsigned char payload_buf[21];
	    	memset(payload_buf, 0, 21);
	    	int cc = recvPacket(socket, payload_buf, 21, src_addr, addrlen);
	    	if (cc < 0 || isMsgSentByMe)
	    		return;

	    	unsigned char name_len = payload_buf[0];
	    	char name[NAMESIZE];
	    	memset(name, 0, NAMESIZE);
	    	memcpy(name, payload_buf + 1, name_len);
	    	p = new JoinMessage(ratId, msgId, name);

	    	recvMsgPrint(p);
    		break;
    	}
    	case JNRS:
    	{
    		unsigned char payload_buf[37];
	    	memset(payload_buf, 0, 37);
	    	int cc = recvPacket(socket, payload_buf, 37, src_addr, addrlen);
	    	if (cc < 0 || isMsgSentByMe)
	    		return;

	    	unsigned char senderId[UUID_SIZE];
	    	memset(senderId, 0, UUID_SIZE);
	    	memcpy(senderId, payload_buf, UUID_SIZE);
	    	unsigned char name_len = payload_buf[17];
	    	char name[NAMESIZE];
	    	memset(name, 0, NAMESIZE);
	    	memcpy(name, payload_buf + 17, name_len);
	    	p = new JoinResponseMessage(ratId, msgId, name, senderId);

	    	recvMsgPrint(p);
    		break;
    	}
    	case KPLV:
    	{
	    	unsigned char payload_buf[14];
	    	memset(payload_buf, 0, 14);
	    	int cc = recvPacket(socket, payload_buf, 14, src_addr, addrlen);
	    	if (cc < 0 || isMsgSentByMe)
	    		return;

			unsigned char ratPosX = payload_buf[0];
			unsigned char ratPosY = payload_buf[1];
			unsigned char ratDir = payload_buf[2];
			int score;
			memcpy(&score, payload_buf + 3, 4);
			unsigned char missileFlag = payload_buf[7];

			// if missileFlag is not zero, set missile info
			if (missileFlag == 0) {
				p = new KeepAliveMessage(ratId, msgId, ratPosX, ratPosY, ratDir, score, missileFlag);	
			} else {
				unsigned char missilePosX = payload_buf[8];
				unsigned char missilePosY = payload_buf[9];
				unsigned int missileSeqNum = 0;
				memcpy(&missileSeqNum, payload_buf + 10, 4);
				p = new KeepAliveMessage(ratId, msgId, ratPosX, ratPosY, ratDir, score, missileFlag, missilePosX, missilePosY, missileSeqNum);
			}
	    	
	    	recvMsgPrint(p);
	    	break;
    	}
    	case LEAV:
    	{
    		if (isMsgSentByMe)
    			return;

    		p = new LeaveMessage(ratId, msgId);

    		recvMsgPrint(p);
    		break;
    	}
    	case HITM:
    	{
    		unsigned char payload_buf[20];
    		memset(payload_buf, 0, 20);
    		int cc = recvPacket(socket, payload_buf, 20, src_addr, addrlen);
    		if (cc < 0)
    			return;

    		unsigned char shooterId[UUID_SIZE];
    		memset(shooterId, 0, UUID_SIZE);
    		memcpy(shooterId, payload_buf, UUID_SIZE);
    		unsigned int missileSeqNum = 0;
    		memcpy(&missileSeqNum, payload_buf + UUID_SIZE, 4);
    		p = new HitMessage(ratId, msgId, shooterId, missileSeqNum);
    		break;
    	}
    	case HTRS:
    	{
    		unsigned char payload_buf[20];
    		memset(payload_buf, 0, 20);
    		int cc = recvPacket(socket, payload_buf, 20, src_addr, addrlen);
    		if (cc < 0)
    			return;

    		unsigned char victimId[UUID_SIZE];
    		memset(victimId, 0, UUID_SIZE);
    		memcpy(victimId, payload_buf, UUID_SIZE);
    		unsigned int missileSeqNum = 0;
    		memcpy(&missileSeqNum, payload_buf + UUID_SIZE, 4);
    		p = new HitResponseMessage(ratId, msgId, victimId, missileSeqNum);
    		break;
    	}
    	default:
    	break;
    }
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own if necessary */
void ConvertOutgoing(Message *p)
{
}

void sendKeepAliveMessage() {
	KeepAliveMessage keepAliveMsg(M->my_ratId.value(), getMessageId(), 
									MY_X_LOC, MY_Y_LOC, MY_DIR, MY_SCORE, 
									MY_MISSILE_EXIST, MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC, MY_MISSILE_SEQNUM);
	sendMsgPrint(&keepAliveMsg);

	unsigned char msg_buf[HEADER_SIZE + 14];
	memset(msg_buf, 0, HEADER_SIZE + 14);
	memcpy(msg_buf, &keepAliveMsg.msgType, 1);
	memcpy(msg_buf + 2, &keepAliveMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &keepAliveMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, &keepAliveMsg.ratPosX, 1);
	memcpy(msg_buf + HEADER_SIZE + 1, &keepAliveMsg.ratPosY, 1);
	memcpy(msg_buf + HEADER_SIZE + 2, &keepAliveMsg.ratDir, 1);
	memcpy(msg_buf + HEADER_SIZE + 3, &keepAliveMsg.score, 4);
	memcpy(msg_buf + HEADER_SIZE + 7, &keepAliveMsg.missileFlag, 1);
	memcpy(msg_buf + HEADER_SIZE + 8, &keepAliveMsg.missilePosX, 1);
	memcpy(msg_buf + HEADER_SIZE + 9, &keepAliveMsg.missilePosY, 1);
	memcpy(msg_buf + HEADER_SIZE + 10, &keepAliveMsg.missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 14, 0, 
			(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendLeaveMessage() {
	LeaveMessage leaveMsg(M->my_ratId.value(), getMessageId());
	sendMsgPrint(&leaveMsg);

	unsigned char msg_buf[HEADER_SIZE];
	memset(msg_buf, 0, HEADER_SIZE);
	memcpy(msg_buf, &leaveMsg.msgType, 1);
	memcpy(msg_buf + 2, &leaveMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &leaveMsg.msgId, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendJoinMessage() {
	JoinMessage joinMsg(M->my_ratId.value(), getMessageId(), M->myName_);

	unsigned char msg_buf[HEADER_SIZE + 21];
	memset(msg_buf, 0, HEADER_SIZE + 21);
	memcpy(msg_buf, &joinMsg.msgType, 1);
	memcpy(msg_buf + 2, &joinMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &joinMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, &joinMsg.len, 1);
	memcpy(msg_buf + HEADER_SIZE + 1, joinMsg.name, joinMsg.len);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 21, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendJoinResponseMessage(unsigned char *senderId) {
	JoinResponseMessage joinResponseMsg(M->my_ratId.value(), getMessageId(), M->myName_, senderId);

	sendMsgPrint(&keepAliveMsg);

	unsigned char msg_buf[HEADER_SIZE + 37];
	memset(msg_buf, 0, HEADER_SIZE + 37);
	memcpy(msg_buf, &joinResponseMsg.msgType, 1);
	memcpy(msg_buf + 2, &joinResponseMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &joinResponseMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, joinResponseMsg.senderId, UUID_SIZE);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &joinResponseMsg.len, 1);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE + 1, joinResponseMsg.name, joinResponseMsg.len);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 37, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendHitMessage(unsigned char *shooterId, unsigned int other_missileSeqNum) {
	HitMessage hitMsg(M->my_ratId.value(), getMessageId(), shooterId, other_missileSeqNum);

	unsigned char msg_buf[HEADER_SIZE + 20];
	memset(msg_buf, 0, HEADER_SIZE + 20);
	memcpy(msg_buf, &hitMsg.msgType, 1);
	memcpy(msg_buf + 2, &hitMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &hitMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, hitMsg.shooterId, UUID_SIZE);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &hitMsg.missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 20, 0, 
		(struct sockaddr *)&groupAddr, sizeof(Sockaddr));
}

void sendHitResponseMessage(unsigned char *victimId, unsigned int other_missileSeqNum) {
	HitResponseMessage hitResponseMsg(M->my_ratId.value(), getMessageId(), victimId, other_missileSeqNum);

	unsigned char msg_buf[HEADER_SIZE + 20];
	memset(msg_buf, 0, HEADER_SIZE + 20);
	memcpy(msg_buf, &hitResponseMsg.msgType, 1);
	memcpy(msg_buf + 2, &hitResponseMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, &hitResponseMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, hitResponseMsg.victimId, UUID_SIZE);
	memcpy(msg_buf + HEADER_SIZE + UUID_SIZE, &hitResponseMsg.missileSeqNum, 4);

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

void myMissileStatusPrint() {
	for (int i = 0; i < 100; i++)
		printf("-");
	printf("\n");
	printf("My Missile Status: \n");
	printf("Exist: %d, X: %d, Y: %d, dir: %d, SeqNum: %d\n", MY_MISSILE_EXIST, MY_MISSILE_X_LOC, MY_MISSILE_Y_LOC, MY_MISSILE_DIR, MY_MISSILE_SEQNUM);
	for (int i = 0; i < 100; i++)
		printf("-");
	printf("\n\n");
}

/* One Rat gets a new message Id */
unsigned int getMessageId() {
	return currentMessageId++;
}

double getCurrentTime() {
	struct timeval tv; 
	memset(&tv, 0, sizeof(struct timeval));
	gettimeofday(&tv, NULL);  	

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

unsigned int getCurrentMissileId() {
	return currentMissileId;
}

void incrCurrentMissileId() {
	++currentMissileId;
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
	if (firstJoinMsgSendTime != 0 && getCurrentTime() - firstJoinMsgSendTime >= JOIN_PHASE_LASTTIME)
		M->myCurrPhaseStateIs(PLAY_PHASE);
}

void playPhase() {
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.begin(); it != M->otherRatInfo_map.end(); ++it) {
		if (it->second.missile.exist == true && it->second.missile.x.value() == MY_X_LOC && it->second.missile.y.value() == MY_Y_LOC) {
			// I am hit by a missile, send HitMessage and go to HIT_PHASE
			// I can only be hit by only one missile at one time
			printf("I am hit by a missile from ratId: ");
			printRatId(it->first.m_ratId);

			M->myCurrPhaseStateIs(HIT_PHASE);
			break;
		}
	}
}

void hitPhase() {

}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
void ratStates()
{
	// go over otherRatInfo_map and print other rats states (score)
	for (map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.begin(); it != M->otherRatInfo_map.end(); ++it) {
		if (strlen(it->second.ratName) > 0) {

		}
	}

}

/* ----------------------------------------------------------------------- */
// This function is only used to update my missile position if the missile exists
void manageMissiles()
{
	// update my missile info once 200ms
	// TODO: when shoot a missile, must update lastMissilePosUpdateTime
	if (MY_MISSILE_EXIST == true) {
		unsigned int step = (getCurrentTime() - lastMissilePosUpdateTime) / MISSILE_UPDATE_INTERVAL;
		if (step > 0) {
			switch(MY_MISSILE_DIR) {
				case NORTH:	M->missileXLocIs(Loc(MY_MISSILE_X_LOC + step)); break;
				case SOUTH:	M->missileXLocIs(Loc(MY_MISSILE_X_LOC - step)); break;
				case EAST:	M->missileYLocIs(Loc(MY_MISSILE_Y_LOC + step)); break;
				case WEST:	M->missileYLocIs(Loc(MY_MISSILE_Y_LOC - step)); break;
				default:
					M->missileExistIs(false);
					incrCurrentMissileId();
					break;
			}

			lastMissilePosUpdateTime = getCurrentTime();
		}	

		// missile hit the wall
		if (M->maze_[MY_MISSILE_X_LOC][MY_MISSILE_Y_LOC]) {
			M->missileExistIs(false);
			incrCurrentMissileId();
		}
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

/* ----------------------------------------------------------------------- */

/* Message has been converted from byte stream to data structure
 * processPacket function is used to update shared state
 */

void processPacket (MWEvent *eventPacket)
{
	Message *msg = eventPacket->eventDetail;

	switch(msg->msgType) {
		case JOIN:
		{
			JoinMessage *joinMsg = (JoinMessage *)msg;
			process_recv_JoinMessage(joinMsg);
			break;
		}
		case JNRS:
		{
			JoinResponseMessage *joinResponseMsg = (JoinResponseMessage *)msg;
			process_recv_JoinResponseMessage(joinResponseMsg);
			break;
		}
		case KPLV:
		{
			KeepAliveMessage *keepAliveMsg = (KeepAliveMessage *)msg;
			process_recv_KeepAliveMessage(keepAliveMsg);
			break;
		}
		case LEAV:
		{
			LeaveMessage *leaveMsg = (LeaveMessage *)msg;
			process_recv_LeaveMessage(leaveMsg);
			break;
		}
		case HITM:
		{
			HitMessage *hitMsg = (HitMessage *)msg;
			process_recv_HitMessage(hitMsg);
			break;
		}
		case HTRS:
		{
			HitResponseMessage *hitResponseMsg = (HitResponseMessage *)msg;
			process_recv_HitResponseMessage(hitResponseMsg);
			break;
		}
	}

	// delete msg pointer should work here
	// delete msg;
}

void process_recv_JoinMessage(JoinMessage *p) {
	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.find(p->ratId);
	if (it != M->otherRatInfo_map.end()) {
		// if find JoinMessage ratId in my otherRatInfo table
		// update thia player's name
		if (!memcmp(it->second.ratName, p->name, NAMESIZE)) {			
			memcpy(it->second.ratName, p->name, NAMESIZE);

			printf("Receive JoinMessage and update ratName: %s, RatId: ", it->second.ratName);
			printRatId(it->first.m_ratId);
		}	
	} else {
		MW_RatId other_ratId(p->ratId);
		OtherRat other;
		memcpy(other.ratName, p->name, NAMESIZE);
		other.score = 0;
		other.lastKeepAliveRecvTime = getCurrentTime();
		M->otherRatInfo_map.insert(pair<MW_RatId, OtherRat>(other_ratId, other));
		
		printf("Receive JoinMessage and store ratName: %s, RatId: ", other.ratName);
		printRatId(other_ratId.value());
	}
}

void process_recv_JoinResponseMessage(JoinResponseMessage *p) {
	// Receiving JoinResponseMessage is valid only in JOIN_PHASE and JoinResponseMessage is intended for me 
	if (M->myCurrPhaseState() == JOIN_PHASE && isRatIdEquals(p->senderId, M->my_ratId.m_ratId)) {
		map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.find(p->ratId);
		if (it != M->otherRatInfo_map.end()) {
			// if find JoinReponseMessage ratId in my otherRatInfo table
			// update this play's name
			if (!memcmp(it->second.ratName, p->name, NAMESIZE)) {			
				memcpy(it->second.ratName, p->name, NAMESIZE);

				printf("Receive JoinResponseMessage and update ratName: %s, RatId: ", it->second.ratName);
				printRatId(it->first.m_ratId);
			}	
		} else {
			MW_RatId other_ratId(p->ratId);
			OtherRat other;
			memcpy(other.ratName, p->name, NAMESIZE);
			other.score = 0;
			other.lastKeepAliveRecvTime = getCurrentTime();
			M->otherRatInfo_map.insert(pair<MW_RatId, OtherRat>(other_ratId, other));
			
			printf("Receive JoinResponseMessage and store ratName: %s, RatId: ", other.ratName);
			printRatId(other_ratId.value());
		}
	}	
}

void process_recv_KeepAliveMessage(KeepAliveMessage *p) {
	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.find(p->ratId);
	if (it != M->otherRatInfo_map.end()) {
		// find send KeepAliveMessage ratId in my otherRatInfo table
		// update other Rat info in my table
		OtherRat *other = &it->second;
		other->rat.x = Loc(p->ratPosX);
		other->rat.y = Loc(p->ratPosY);
		other->rat.dir = Direction(p->ratDir);
		other->missile.exist = p->missileFlag;
		other->missile.x = Loc(p->missilePosX);
		other->missile.y = Loc(p->missilePosY);
		other->missile.seqNum = p->missileSeqNum;
		other->score = p->score;
		other->lastKeepAliveRecvTime = getCurrentTime();
	} else {
		MW_RatId other_ratId(p->ratId);
		OtherRat other;
		other.rat.x = Loc(p->ratPosX);
		other.rat.y = Loc(p->ratPosY);
		other.rat.dir = Direction(p->ratDir);
		other.missile.exist = p->missileFlag;
		other.missile.x = Loc(p->missilePosX);
		other.missile.y = Loc(p->missilePosY);
		other.missile.seqNum = p->missileSeqNum;
		other.score = p->score;
		other.lastKeepAliveRecvTime = getCurrentTime();
		M->otherRatInfo_map.insert(pair<MW_RatId, OtherRat>(other_ratId, other));
	}
}

void process_recv_LeaveMessage(LeaveMessage *p) {
	map<MW_RatId, OtherRat>::iterator it = M->otherRatInfo_map.find(p->ratId);
	if (it != M->otherRatInfo_map.end()) {
		// find sent LeaveMessage ratId in my otherRatInfo table
		// remove this rat info from my table
		printf("Remove rat with ratId: ");
		printRatId(it->first.m_ratId);

		printf("Before remove otherRatInfo_map size: %d\n", (unsigned int)M->otherRatInfo_map.size());
		MW_RatId other_ratId(p->ratId);
		M->otherRatInfo_map.erase(other_ratId);
		printf("After remove otherRatInfo_map size: %d\n", (unsigned int)M->otherRatInfo_map.size());

	}
}

void process_recv_HitMessage(HitMessage *p) {
	if (isRatIdEquals(p->shooterId, M->my_ratId.value())) {
		M->scoreIs( M->score().value() + 11 );
		// store my missile with some seqNum hit someone

		sendHitResponseMessage(p->ratId, p->missileSeqNum);
	}
}

void process_recv_HitResponseMessage(HitResponseMessage *p) {
	if (isRatIdEquals(p->victimId, M->my_ratId.value())) {
		M->scoreIs( M->score().value() - 5 );

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
