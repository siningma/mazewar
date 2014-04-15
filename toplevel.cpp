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

double lastKeepAliveMsgSendTime = 0;

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

    JoinMessage join(getMessageId(), "sma");
    printf("Test Join Message name: %s, messageType: %x, msgId: %d\n", join.name.c_str(), join.msgType, join.msgId);

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
		// send keep alive message every 200ms
		double currentTimestamp = getCurrentTime();
		if (currentTimestamp - lastKeepAliveMsgSendTime >= KEEPALIVE_INTERVAL) {
			sendKeepAliveMessage();
			lastKeepAliveMsgSendTime = getCurrentTime();
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
	int cc = recvfrom(socket, payload_buf, len, 0,
		        src_addr, addrlen);
	if (cc <= 0) {
	    if (cc < 0 && errno != EINTR) 
			perror("event recvfrom");
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

	printf("Message Header detail: \n");
	printf("Message type: %x\n", msgType);
	printf("RatId: ");
    for (int i = 2 ; i < 2 + UUID_SIZE; i++) {
    	printf("%x", ratId[i]);
    }
    printf("\n");
    printf("Message Id: %u\n", msgId);

    switch (msgType) {
    	int cc;
    	case JOIN:
    	{
    		break;
    	}
    	case JNRS:
    	{
    		break;
    	}
    	case KPLV:
    	{
	    	unsigned char payload_buf[14];
	    	memset(payload_buf, 0, 14);
	    	int cc = recvPacket(socket, payload_buf, 14, src_addr, addrlen);
	    	if (cc < 0)
	    		return;

			unsigned char ratPosX = payload_buf[0];
			unsigned char ratPosY = payload_buf[1];
			unsigned char ratDir = payload_buf[2];
			int score;
			memcpy(&score, payload_buf + 3, 4);
			unsigned char missileFlag = payload_buf[7];

			// if missileFlag is not zero, set missile info
			unsigned char missilePosX = 0;
			unsigned char missilePosY = 0;
			unsigned int missileSeqNum = 0;
			if (missileFlag != 0) {
				missilePosX = payload_buf[8];
				missilePosY = payload_buf[9];
				memcpy(&missileSeqNum, payload_buf + 10, 4);
			}
	    	p = new KeepAliveMessage(msgId, ratPosX, ratPosY, ratDir, score, missileFlag, missilePosX, missilePosY, missileSeqNum);
	    	p->print();
	    	break;
    	}
    	case LEAV:
    	{
    		p = new LeaveMessage(msgId);
    		p->print();
    		break;
    	}
    	case HITM:
    	{
    		unsigned char payload_buf[20];
    		memset(payload_buf 0, 20);
    		int cc = recvPacket(socket, payload_buf, 20, src_addr, addrlen);
    		if (cc < 0)
    			return;

    		unsigned char shooterId[UUID_SIZE];
    		memset(shooterId, 0, UUID_SIZE);
    		memcpy(shooterId, payload_buf, UUID_SIZE);
    		unsigned int missileSeqNum = 0;
    		memcpy(&missileSeqNum, payload_buf + UUID_SIZE, 4);
    		p = new HitMessage(msgId, missileSeqNum, shooterId);
    		break;
    	}
    	case HTRS:
    	{
    		unsigned char payload_buf[20];
    		memset(payload_buf 0, 20);
    		int cc = recvPacket(socket, payload_buf, 20, src_addr, addrlen);
    		if (cc < 0)
    			return;

    		unsigned char victimId[UUID_SIZE];
    		memset(victimId, 0, UUID_SIZE);
    		memcpy(victimId, payload_buf, UUID_SIZE);
    		unsigned int missileSeqNum = 0;
    		memcpy(&missileSeqNum, payload_buf + UUID_SIZE, 4);
    		p = new HitResponseMessage(msgId, missileSeqNum, victimId);
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
	KeepAliveMessage keepAliveMsg(getMessageId(), MY_X_LOC, MY_Y_LOC, MY_DIR, MY_SCORE);
	keepAliveMsg.print();

	unsigned char msg_buf[HEADER_SIZE + 14];
	memset(msg_buf, 0, HEADER_SIZE + 14);
	memcpy(msg_buf, keepAliveMsg.msgType, 1);
	memcpy(msg_buf + 2, keepAliveMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, keepAliveMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, keepAliveMsg.ratPosX, 1);
	memcpy(msg_buf + HEADER_SIZE + 1, keepAliveMsg.ratPosY, 1);
	memcpy(msg_buf + HEADER_SIZE + 2, keepAliveMsg.ratDir, 1);
	memcpy(msg_buf + HEADER_SIZE + 3, keepAliveMsg.score, 4);
	memcpy(msg_buf + HEADER_SIZE + 7, keepAliveMsg.missileFlag, 1);
	memcpy(msg_buf + HEADER_SIZE + 8, keepAliveMsg.missilePosX, 1);
	memcpy(msg_buf + HEADER_SIZE + 9, keepAliveMsg.missilePosY, 1);
	memcpy(msg_buf + HEADER_SIZE + 10, keepAliveMsg.missileSeqNum, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 14, 0, 
			(struct sockaddr *)M->myAddr(), sizeof(*M->myAddr()));
}

void sendLeaveMessage() {
	LeaveMessage leaveMsg(getMessageId());

	unsigned char msg_buf[HEADER_SIZE];
	memset(msg_buf, 0, HEADER_SIZE);
	memcpy(msg_buf, leaveMsg.msgType, 1);
	memcpy(msg_buf + 2, leaveMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, leaveMsg.msgId, 4);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE, 0, 
		(struct sockaddr *)M->myAddr(), sizeof(*M->myAddr()));
}

void sendJoinMessage() {
	JoinMessage joinMsg(getMessageId(), M->myName_);

	unsigned char msg_buf[HEADER_SIZE + 21];
	memset(msg_buf, 0, HEADER_SIZE + 21);
	memcpy(msg_buf, joinMsg.msgType, 1);
	memcpy(msg_buf + 2, joinMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, joinMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, joinMsg.len, 1);
	memcpy(msg_buf + HEADER_SIZE + 1, joinMsg.name.c_str(), 20);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 21, 0, 
		(struct sockaddr *)M->myAddr(), sizeof(*M->myAddr()));
}

void sendJoinResponseMessage() {
	JoinResponseMessage joinResponseMsg(getMessageId(), M->myName_);

	unsigned char msg_buf[HEADER_SIZE + 21];
	memset(msg_buf, 0, HEADER_SIZE + 21);
	memcpy(msg_buf, joinResponseMsg.msgType, 1);
	memcpy(msg_buf + 2, joinResponseMsg.ratId, UUID_SIZE);
	memcpy(msg_buf + 2 + UUID_SIZE, joinResponseMsg.msgId, 4);
	memcpy(msg_buf + HEADER_SIZE, joinResponseMsg.len, 1);
	memcpy(msg_buf + HEADER_SIZE + 1, joinResponseMsg.name.c_str(), 20);

	sendto(M->theSocket(), msg_buf, HEADER_SIZE + 21, 0, 
		(struct sockaddr *)M->myAddr(), sizeof(*M->myAddr()));	
}

void sendHitMessage() {

}

void sendHitResponseMessage() {

}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
void ratStates()
{
  /* In our sample version, we don't know about the state of any rats over
     the net, so this is a no-op */
	KeepAliveMessage *msgAlive = new KeepAliveMessage(getMessageId(), MY_X_LOC, MY_Y_LOC, MY_DIR, M->score().value());
	sendPacketToPlayer(MY_RAT_INDEX, msgAlive);
}

/* ----------------------------------------------------------------------- */

/* This is just for the sample version, rewrite your own */
void manageMissiles()
{
  /* Leave this up to you. */
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
	switch(msg->msgType)
	{
		case KPLV:
		{
			KeepAliveMessage *KPLVmsg = (KeepAliveMessage *)msg;
			KPLVmsg->print();
			break;
		}
		default:
			printf("unexpected msgType:%x\n", msg->msgType);
	}
}

/* ----------------------------------------------------------------------- */

/* Message has been converted from byte stream to data structure
 * processPacket function is used to update shared state
 */

void processPacket (MWEvent *eventPacket)
{
/*
	MW244BPacket		*pack = eventPacket->eventDetail;
	DataStructureX		*packX;

	switch(pack->type) {
	case PACKET_TYPE_X:
	  packX = (DataStructureX *) &(pack->body);
	  break;
        case ...
	}
*/
	Message *msg = eventPacket->eventDetail;

	switch(msg->msgType) {
		case JOIN:
		{
			JoinMessage *joinMsg = (JoinMessage *)msg;
		
			break;
		}
		case JNRS:
		{
			JoinResponseMessage *joinResponseMsg = (JoinResponseMessage *)msg;

			break;
		}
		case KPLV:
		{
			KeepAliveMessage *keepAliveMsg = (KeepAliveMessage *)msg;

			break;
		}
		case LEAV:
		{
			LeaveMessage *leaveMsg = (LeaveMessage *)msg;

			break;
		}
		case HITM:
		{
			HitMessage *hitMsg = (HitMessage *)msg;

			break;
		}
		case HTRS:
		{
			HitResponseMessage *hitResponseMsg = (HitResponseMessage *)msg;

			break;
		}
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
