/* $Header: mazewar.h,v 1.7 88/08/25 09:59:51 kent Exp $ */

/*
 * mazewar.h - Definitions for MazeWar
 *
 * Author:	Christopher A. Kent
 * 		Western Research Laboratory
 * 		Digital Equipment Corporation
 * Date:	Wed Sep 24 1986
 */

/* Modified by Michael Greenwald for CS244B, Mar 1992,
   Greenwald@cs.stanford.edu */

/* Modified by Nicholas Dovidio for CS244B, Mar 2009,
 * ndovidio@stanford.edu
 * This version now uses the CS249a/b style of C++ coding.
 */

/***********************************************************
Copyright 1986 by Digital Equipment Corporation, Maynard, Massachusetts,

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the names of Digital not be
used in advertising or publicity pertaining to disstribution of the
software without specific, written prior permission.

DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

******************************************************************/

#ifndef MAZEWAR_H
#define MAZEWAR_H


#include "fwk/NamedInterface.h"

#include "Nominal.h"
#include "Exception.h"
#include <string>
#include <map>
#include <sys/time.h>

#include <boost/uuid/uuid_io.hpp>         // streaming operators etc.
#include <boost/uuid/uuid_generators.hpp> // generators
/* fundamental constants */

#ifndef	TRUE
#define	TRUE		1
#define	FALSE		0
#endif	/* TRUE */

/* You can modify this if you want to */
#define	MAX_RATS	8

/* network stuff */
/* Feel free to modify.  This is the simplest version we came up with */

/* A unique MAZEPORT will be assigned to your team by the TA */
#define	MAZEPORT	5010
/* The multicast group for Mazewar is 224.1.1.1 */
#define MAZEGROUP       0xe0010101
#define	MAZESERVICE	"mazewar244B"

/* The next two >must< be a power of two, because we subtract 1 from them
   to get a bitmask for random()
 */
#define	MAZEXMAX	32
#define	MAZEYMAX	16
#define	VECTORSIZE	55
#define	NAMESIZE	20
#define	NDIRECTION	4
#define	NORTH		0
#define	SOUTH		1
#define	EAST		2
#define	WEST		3
#define	NVIEW		4
#define	LEFT		0
#define	RIGHT		1
#define	REAR		2
#define	FRONT		3


#define JOIN 	0xE0    	/* Join Message type */
#define JNRS 	0xE1        /* Join Response Message type */
#define KPLV 	0xE2		/* KeepAlive Message type */
#define LEAV	0xE3		/* Leave Message type */
#define HITM 	0xE4		/* Hit Message type */
#define HTRS 	0xE5		/* Hit Response Message type */

#define UUID_SIZE	16 		/* UUID size for ratId */
#define HEADER_SIZE	22 		/* Header size */
#define KEEPALIVE_INTERVAL 200

/* types */

typedef	struct sockaddr_in			Sockaddr;
typedef bool	               		MazeRow[MAZEYMAX];
typedef	MazeRow						MazeType [MAZEXMAX];
typedef	MazeRow						*MazeTypePtr;
//typedef	short						Direction;
typedef	struct {short	x, y; }		XYpoint;
typedef	struct {XYpoint	p1, p2;}	XYpair;
typedef	struct {short	xcor, ycor;}XY;
typedef	struct {unsigned short	bits[16];}	BitCell;
typedef	char						RatName[NAMESIZE];


 	class Direction : public Ordinal<Direction, short> {
	public:
		Direction(short num) : Ordinal<Direction, short>(num) {
			if(num<NORTH || num>NDIRECTION){
				throw RangeException("Error: Unexpected value.\n");
			}
		}
	};

 	class Loc : public Ordinal<Loc, short> {
	public:
		Loc(short num) : Ordinal<Loc, short>(num) {
			if(num<0){
				throw RangeException("Error: Unexpected negative value.\n");
			}
		}
	};

 	class Score : public Ordinal<Score, int> {
	public:
		Score(int num) : Ordinal<Score, int>(num) {}
	};


 	class RatIndexType : public Ordinal<RatIndexType, int> {
	public:
		RatIndexType(int num) : Ordinal<RatIndexType, int>(num) {
			if(num<0){
				throw RangeException("Error: Unexpected negative value.\n");
			}
		}
	};

 	class RatId : public Ordinal<RatId, unsigned short> {
	public:
		RatId(unsigned short num) : Ordinal<RatId, unsigned short>(num) {
		}
	};

 	class TokenId : public Ordinal<TokenId, long> {
	public:
		TokenId(long num) : Ordinal<TokenId, long>(num) {}
	};


class RatAppearance{

	public:
		RatAppearance() :  x(1), y(1), tokenId(0) {};
		bool	visible;
		Loc	x, y;
		short	distance;
		TokenId	tokenId;
};

class Rat{

public:
	Rat() :  playing(0), x(1), y(1), dir(NORTH){};
	bool playing;
	Loc	x, y;
	Direction dir;
};

class Missile{
public:
	Missile() : exist(FALSE), x(0), y(0), dir(NORTH) {};
	bool exist;
	Loc x, y;
	Direction dir;
};

class MW_RatId{
public:
	unsigned char m_ratId[UUID_SIZE];
	
	MW_RatId() { memset(m_ratId, 0, UUID_SIZE); }
	MW_RatId(unsigned char* ratId) {
		memset(m_ratId, 0, UUID_SIZE);
		memcpy(m_ratId, ratId, UUID_SIZE);
	} 
	MW_RatId(const MW_RatId& other) {
		memset(this->m_ratId, 0, UUID_SIZE);
		memcpy(this->m_ratId, other.m_ratId, UUID_SIZE);
	}
	MW_RatId& operator= (const MW_RatId& other) {
		if(this == &other)	return *this;
	
		memset(this->m_ratId, 0, UUID_SIZE);
		memcpy(this->m_ratId, other.m_ratId, UUID_SIZE);
		return *this;
	}
	bool operator<(const MW_RatId& other) const {
		return (memcmp(this->m_ratId, other.m_ratId, UUID_SIZE) < 0);
	}
};

typedef struct {
	MW_RatId ratId;
	std::string ratName;
	Rat rat;
	Missile missile;
	int score;
	int lastKeepAliveTime;
}	PlayerInfo;

typedef	RatAppearance			RatApp_type [MAX_RATS];
typedef	RatAppearance *			RatLook;

/* defined in display.c */
extern RatApp_type 			Rats2Display;

/* variables "exported" by the mazewar "module" */
class MazewarInstance :  public Fwk::NamedInterface  {
 public:
    typedef Fwk::Ptr<MazewarInstance const> PtrConst;
    typedef Fwk::Ptr<MazewarInstance> Ptr;

	static MazewarInstance::Ptr mazewarInstanceNew(string s){
      MazewarInstance * m = new MazewarInstance(s);
      return m;
    }

    inline Direction dir() const { return dir_; }
    void dirIs(Direction dir) { this->dir_ = dir; }
    inline Direction dirPeek() const { return dirPeek_; }
    void dirPeekIs(Direction dirPeek) { this->dirPeek_ = dirPeek; }

    inline long mazePort() const { return mazePort_; }
    void mazePortIs(long  mazePort) { this->mazePort_ = mazePort; }
    inline Sockaddr* myAddr() const { return myAddr_; }
    void myAddrIs(Sockaddr *myAddr) { this->myAddr_ = myAddr; }
    inline RatId myRatId() const { return myRatId_; }
    void myRatIdIs(RatId myRatId) { this->myRatId_ = myRatId; }

    inline bool peeking() const { return peeking_; }
    void peekingIs(bool peeking) { this->peeking_ = peeking; }
    inline int theSocket() const { return theSocket_; }
    void theSocketIs(int theSocket) { this->theSocket_ = theSocket; }
    inline Score score() const { return score_; }
    void scoreIs(Score score) { this->score_ = score; }
    inline Loc xloc() const { return xloc_; }
    void xlocIs(Loc xloc) { this->xloc_ = xloc; }
    inline Loc yloc() const { return yloc_; }
    void ylocIs(Loc yloc) { this->yloc_ = yloc; }
    inline Loc xPeek() const { return xPeek_; }
    void xPeekIs(Loc xPeek) { this->xPeek_ = xPeek; }
    inline Loc yPeek() const { return yPeek_; }
    void yPeekIs(Loc yPeek) { this->yPeek_ = yPeek; }
    inline int active() const { return active_; }
    void activeIs(int active) { this->active_ = active; }
    inline Rat rat(RatIndexType num) const { return mazeRats_[num.value()]; }
    void ratIs(Rat rat, RatIndexType num) { this->mazeRats_[num.value()] = rat; }

    MazeType maze_;
    RatName myName_;
protected:
	MazewarInstance(string s) : Fwk::NamedInterface(s), dir_(0), dirPeek_(0), myRatId_(0), score_(0),
		xloc_(1), yloc_(3), xPeek_(0), yPeek_(0) {
		myAddr_ = (Sockaddr*)malloc(sizeof(Sockaddr));
		if(!myAddr_) {
			printf("Error allocating sockaddr variable");
		}
	}
	Direction	dir_;
    Direction dirPeek_;

    long mazePort_;
    Sockaddr *myAddr_;
    Rat mazeRats_[MAX_RATS];
    RatId myRatId_;

    bool peeking_;
    int theSocket_;
    Score score_;
    Loc xloc_;
    Loc yloc_;
    Loc xPeek_;
    Loc yPeek_;
    int active_;
};
extern MazewarInstance::Ptr M;

#define MY_RAT_INDEX		0
#define MY_DIR			M->dir().value()
#define MY_X_LOC		M->xloc().value()
#define MY_Y_LOC		M->yloc().value()
#define MY_SCORE		M->score().value()

/* events */

#define	EVENT_A		1		/* user pressed "A" */
#define	EVENT_S		2		/* user pressed "S" */
#define	EVENT_F		3		/* user pressed "F" */
#define	EVENT_D		4		/* user pressed "D" */
#define	EVENT_BAR	5		/* user pressed space bar */
#define	EVENT_LEFT_D	6		/* user pressed left mouse button */
#define	EVENT_RIGHT_D	7		/* user pressed right button */
#define	EVENT_MIDDLE_D	8		/* user pressed middle button */
#define	EVENT_LEFT_U	9		/* user released l.M.b */
#define	EVENT_RIGHT_U	10		/* user released r.M.b */

#define	EVENT_NETWORK	16		/* incoming network packet */
#define	EVENT_INT	17		/* user pressed interrupt key */
#define	EVENT_TIMEOUT	18		/* nothing happened! */

extern unsigned short	ratBits[];
/* replace this with appropriate definition of your own */
/*typedef	struct {
	unsigned char type;
	u_long	body[256];
}					MW244BPacket; */

static unsigned int currentMessageId = 0;
/* One Rat(Player) gets a new message Id */
static unsigned int getMessageId() {
	return currentMessageId++;
}

/* Common message header for all messages */
class Message {
public:
	unsigned char msgType;
	unsigned char reserved;
	unsigned char ratId[UUID_SIZE + 1];
	unsigned int msgId;

	Message() {}

	Message(unsigned char msgType, unsigned int msgId) : reserved(0) {
		this->msgType = msgType;
		this->msgId = msgId;

		// generate random UUID
	    memset(this->ratId, 0, UUID_SIZE + 1);
	    boost::uuids::uuid uuid = boost::uuids::random_generator()();
	    memcpy(this->ratId, &uuid, UUID_SIZE);
	}

	void print() {
		printf("Message type: %x\n", msgType);
		printf("RatId: ");
	    for (int i = 2 ; i < 2 + UUID_SIZE; i++) {
	    	printf("%x", ratId[i]);
	    }
	    printf("\n");
	    printf("Message Id: %u\n", msgId);
	}
};

/* Join message struct */
class JoinMessage: public Message {
public:
	unsigned char len;
	std::string name;	

	JoinMessage(unsigned int msgId, std::string name): Message(JOIN, msgId) {
		this->len = name.length();
		this->name = name;
	}
};

/* Join Response message struct */
class JoinResponseMessage: public Message {
public:
	unsigned char senderId[UUID_SIZE + 1];
	unsigned char len;
	std::string name;

	JoinResponseMessage(unsigned int msgId, std::string name, unsigned char* senderId): Message(JNRS, msgId) {
		this->len = name.length();
		this->name = name;
		memset(this->senderId, 0, UUID_SIZE + 1);
		memcpy(this->senderId, senderId, UUID_SIZE);	
	}	
};

/* KeepAlive message struct */
class KeepAliveMessage: public Message {
public:
	unsigned char ratPosX;
	unsigned char ratPosY;
	unsigned char ratDir;
	int score;
	unsigned char missileFlag;
	unsigned char missilePosX;
	unsigned char missilePosY;
	unsigned int missileSeqNum;

	KeepAliveMessage(unsigned int msgId, unsigned char ratPosX, unsigned char ratPosY, unsigned char ratDir, int score,
					unsigned char missileFlag = 0, unsigned char missilePosX = 0, unsigned char missilePosY = 0, unsigned int missileSeqNum = 0)
					: Message(KPLV, msgId) {
		this->ratPosX = ratPosX;
		this->ratPosY = ratPosY;
		this->ratDir = ratDir;
		this->score = score;
		this->missileFlag = missileFlag;
		this->missilePosX = missilePosX;
		this->missilePosY = missilePosY;
		this->missileSeqNum = missileSeqNum;
	}

	void print() {
		Message::print();
		printf("ratPosX: 0x%x, ratPosY: 0x%x, ratDir: 0x%x\n", ratPosX, ratPosY, ratDir);
		printf("score: %d, missileFlag: 0x%x, missilePosX: 0x%x, missilePosY: 0x%x, missileSeqNum: %d\n", score, missileFlag, missilePosX, missilePosY, missileSeqNum);
	}
};

/* Leave message struct */
class LeaveMessage: public Message {
public:
	LeaveMessage(unsigned int msgId): Message(LEAV, msgId) {}
};

/* Hit message struct */
class HitMessage: public Message {
public:
	unsigned char shooterId[UUID_SIZE + 1];
	unsigned int missileSeqNum;

	HitMessage(unsigned int msgId, unsigned int missileSeqNum, unsigned char* shooterId): Message(HITM, msgId) {
		this->missileSeqNum = missileSeqNum;
		memset(this->shooterId, 0, UUID_SIZE + 1);
		memcpy(this->shooterId, shooterId, UUID_SIZE);
	}

	void print() {
		Message::print();

	}
};

/* Hit Response message struct */
class HitResponseMessage: public Message {
public:
	unsigned char victimId[UUID_SIZE + 1];
	unsigned int missileSeqNum;

	HitResponseMessage(unsigned int msgId, unsigned int missileSeqNum, unsigned char* victimId): Message(HTRS, msgId) {
		this->missileSeqNum = missileSeqNum;
		memset(this->victimId, 0, UUID_SIZE + 1);
		memcpy(this->victimId, victimId, UUID_SIZE);
	}
};

typedef	struct {
	short		eventType;
	Message	*eventDetail;	/* for incoming data */
	Sockaddr	eventSource;
}					MWEvent;

static double getCurrentTime() {
	struct timeval tv; 
	memset(&tv, 0, sizeof(struct timeval));
	gettimeofday(&tv, NULL);  	

	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void		*malloc();
Sockaddr	*resolveHost();

/* display.c */
void InitDisplay(int, char **);
void StartDisplay(void);
void ShowView(Loc, Loc, Direction);
void SetMyRatIndexType(RatIndexType);
void SetRatPosition(RatIndexType, Loc, Loc, Direction);
void ClearRatPosition(RatIndexType);
void ShowPosition(Loc, Loc, Direction);
void ShowAllPositions(void);
void showMe(Loc, Loc, Direction);
void clearPosition(RatIndexType, Loc, Loc);
void clearSquare(Loc xClear, Loc yClear);
void NewScoreCard(void);
void UpdateScoreCard(RatIndexType);
void FlipBitmaps(void);
void bitFlip(BitCell *, int size);
void SwapBitmaps(void);
void byteSwap(BitCell *, int size);


/* init.c */
void MazeInit(int, char **);
void ratStates(void);
void getMaze(void);
void setRandom(void);
void getName(char *, char **);
void getString(char *, char **);
void getHostName(char *, char **, Sockaddr *);
Sockaddr *resolveHost(char *);
bool emptyAhead();
bool emptyRight();
bool emptyLeft();
bool emptyBehind();

/* toplevel.c */
void play(void);
void aboutFace(void);
void leftTurn(void);
void rightTurn(void);
void forward(void);
void backward(void);
void peekLeft(void);
void peekRight(void);
void peekStop(void);
void shoot(void);
void quit(int);
void NewPosition(MazewarInstance::Ptr M);
void MWError(char *);
Score GetRatScore(RatIndexType);
char  *GetRatName(RatIndexType);
void ConvertIncoming(Message *p, int socket, const char* header_buf, struct sockaddr *src_addr, socklen_t *addrlen);
void ConvertOutgoing(Message *);
void ratState(void);
void manageMissiles(void);
void DoViewUpdate(void);
void sendPacketToPlayer(RatId, Message *);
void processPacket(MWEvent *);
void netInit(void);
void sendKeepAliveMessage();
void sendLeaveMessage();
void sendJoinMessage();
void sendJoinResponseMessage();
void sendHitMessage();
void sendHitResponseMessage();


/* winsys.c */
void InitWindow(int, char **);
void StartWindow(int, int);
void ClearView(void);
void DrawViewLine(int, int, int, int);
void NextEvent(MWEvent *, int);
bool KBEventPending(void);
void HourGlassCursor(void);
void RatCursor(void);
void DeadRatCursor(void);
void HackMazeBitmap(Loc, Loc, BitCell *);
void DisplayRatBitmap(int, int, int, int, int, int);
void WriteScoreString(RatIndexType);
void ClearScoreLine(RatIndexType);
void InvertScoreLine(RatIndexType);
void NotifyPlayer(void);
void DrawString(const char*, uint32_t, uint32_t, uint32_t);
void StopWindow(void);


#endif
