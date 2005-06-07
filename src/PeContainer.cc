#include <iostream> 
#include <sstream> 

#include "y2storage/PeContainer.h"
#include "y2storage/SystemCmd.h"
#include "y2storage/AppUtil.h"
#include "y2storage/Storage.h"

using namespace std;
using namespace storage;

PeContainer::PeContainer( Storage * const s, CType t ) :
    Container(s,"",t)
    {
    y2milestone( "constructing pe container type %d", t );
    init();
    }

PeContainer::~PeContainer()
    {
    y2milestone( "destructed pe container %s", dev.c_str() );
    }

int
PeContainer::setPeSize( unsigned long long peSizeK, bool lvm1 )
    {
    int ret = 0;
    y2milestone( "peSize:%llu", peSizeK );
    
    if( lvm1 )
	{
	if( peSizeK<8 || peSizeK>16*1024*1024 )
	    ret = PEC_PE_SIZE_INVALID;
	}
    if( ret==0 )
	{
	unsigned long long sz = peSizeK;
	while( sz>1 && sz%2==0 )
	    sz /= 2;
	if( sz!=1 )
	    ret = PEC_PE_SIZE_INVALID;
	}
    if( ret==0 )
	{
	pe_size = peSizeK;
	}
    y2milestone( "ret:%d", ret );
    return( ret );
    }

int
PeContainer::tryUnusePe( const string& dev, list<Pv>& pl, list<Pv>& pladd, 
		         list<Pv>& plrem, unsigned long& removed_pe )
    {
    int ret = 0;
    int added_pv = false;
    Pv cur_pv;
    list<Pv>::iterator cur;
    cur = find( pl.begin(), pl.end(), dev );
    if( cur==pl.end() )
	{
	cur = find( pladd.begin(), pladd.end(), dev );
	if( cur!=pladd.end() )
	    added_pv = true;
	else
	    ret = PEC_PV_NOT_FOUND;
	}
    if( ret==0 )
	cur_pv = *cur;
    if( ret==0 && cur->free_pe<cur->num_pe )
	{
	list<Dm*> li;
	VolPair lp=volPair(Volume::notDeleted);
	VolIterator i=lp.begin();
	while( ret==0 && i!=lp.end() )
	    {
	    Dm* dm = static_cast<Dm*>(&(*i));
	    if( dm->usingPe( dev )>0 )
		{
		if( i->created() )
		    li.push_back( dm );
		else
		    ret = PEC_REMOVE_PV_IN_USE;
		}
	    ++i;
	    }
	list<Dm*>::iterator lii=li.begin(); 
	if( ret==0 )
	    {
	    while( ret==0 && lii!=li.end() )
		{
		map<string,unsigned long> pe_map = (*lii)->getPeMap();
		ret = remLvPeDistribution( (*lii)->getLe(), pe_map, pl, pladd );
		++lii;
		}
	    }
	if( ret==0 )
	    {
	    if( added_pv )
		pladd.erase( cur );
	    else
		pl.erase( cur );
	    lii=li.begin(); 
	    while( ret==0 && lii!=li.end() )
		{
		map<string,unsigned long> pe_map;
		ret = addLvPeDistribution( (*lii)->getLe(), (*lii)->stripes(),
		                           pl, pladd, pe_map );
		if( ret==0 )
		    {
		    (*lii)->setPeMap( pe_map );
		    }
		else
		    {
		    ret = PEC_REMOVE_PV_SIZE_NEEDED;
		    }
		++lii;
		}
	    }
	}
    else if( ret==0 )
	{
	if( added_pv )
	    pladd.erase( cur );
	else
	    pl.erase( cur );
	}
    if( ret==0 )
	{
	getStorage()->setUsedBy( dev, UB_NONE, "" );
	removed_pe += cur_pv.num_pe;
	if( !added_pv )
	    plrem.push_back( cur_pv );
	}
    y2milestone( "ret:%d removed_pe:%lu dev:%s", ret, removed_pe, 
                 cur_pv.device.c_str() );
    return( ret );
    }

int
PeContainer::addLvPeDistribution( unsigned long le, unsigned stripe, list<Pv>& pl, 
				  list<Pv>& pladd, map<string,unsigned long>& pe_map )
    {
    int ret=0;
    y2milestone( "le:%lu stripe:%u", le, stripe );
    map<string,unsigned long>::iterator mit;
    list<Pv>::iterator i;
    if( stripe>1 )
	{
	// this is only a very rough estimate if the creation of the striped 
	// lv may be possible, no sense in emulating LVM allocation strategy
	// here
	unsigned long per_stripe = (le+stripe-1)/stripe;
	list<Pv> tmp = pl;
	tmp.insert( tmp.end(), pladd.begin(), pladd.end() );
	tmp.sort( Pv::comp_le );
	tmp.remove_if( Pv::no_free );
	while( per_stripe>0 && tmp.size()>=stripe )
	    {
	    i = tmp.begin();
	    unsigned rem = min( per_stripe, i->free_pe );
	    for( unsigned cnt=0; cnt<stripe; cnt++ )
		{
		i->free_pe -= rem;
		if( (mit=pe_map.find( i->device ))==pe_map.end() )
		    pe_map[i->device] = rem;
		else
		    mit->second += rem;
		++i;
		}
	    per_stripe -= rem;
	    tmp.remove_if( Pv::no_free );
	    }
	if( per_stripe>0 )
	    ret = PEC_LV_NO_SPACE_STRIPED;
	else
	    {
	    list<Pv>::iterator p;
	    i = pl.begin();
	    while( i!=pl.end() )
		{
		if( (p=find( tmp.begin(), tmp.end(), i->device))!=tmp.end())
		    i->free_pe = p->free_pe;
		else
		    i->free_pe = 0;
		++i;
		}
	    i = pladd.begin();
	    while( i!=pladd.end() )
		{
		if( (p=find( tmp.begin(), tmp.end(), i->device))!=tmp.end())
		    i->free_pe = p->free_pe;
		else
		    i->free_pe = 0;
		++i;
		}
	    }
	}
    else
	{
	unsigned long rest = le;
	i = pl.begin();
	while( rest>0 && i!=pl.end() )
	    {
	    rest -= min(rest,i->free_pe);
	    ++i;
	    }
	i = pladd.begin();
	while( rest>0 && i!=pladd.end() )
	    {
	    rest -= min(rest,i->free_pe);
	    ++i;
	    }
	if( rest>0 )
	    ret = PEC_LV_NO_SPACE_SINGLE;
	else
	    {
	    rest = le;
	    i = pl.begin();
	    while( rest>0 && i!=pl.end() )
		{
		unsigned long tmp = min(rest,i->free_pe);
		i->free_pe -= tmp;
		rest -= tmp;
		if( (mit=pe_map.find( i->device ))==pe_map.end() )
		    pe_map[i->device] = tmp;
		else
		    mit->second += tmp;
		++i;
		}
	    i = pladd.begin();
	    while( rest>0 && i!=pladd.end() )
		{
		unsigned long tmp = min(rest,i->free_pe);
		i->free_pe -= tmp;
		rest -= tmp;
		if( (mit=pe_map.find( i->device ))==pe_map.end() )
		    pe_map[i->device] = tmp;
		else
		    mit->second += tmp;
		++i;
		}
	    }
	}
    if( ret==0 )
	{
	y2mil( "pe_map:" << pe_map );
	}
    y2milestone( "ret:%d", ret );
    return( ret );
    }

int
PeContainer::remLvPeDistribution( unsigned long le, map<string,unsigned long>& pe_map, 
				  list<Pv>& pl, list<Pv>& pladd )
    {
    int ret=0;
    y2mil( "le:" << le << " pe_map:" << pe_map );
    list<Pv>::iterator p;
    map<string,unsigned long>::iterator mit = pe_map.begin();
    while( le>0 && ret==0 && mit != pe_map.end() )
	{
	if( (p=find( pl.begin(), pl.end(), mit->first))!=pl.end() ||
	    (p=find( pladd.begin(), pladd.end(), mit->first))!=pladd.end())
	    {
	    int tmp = min(le,mit->second);
	    p->free_pe += tmp;
	    mit->second -= tmp;
	    le -= tmp;
	    }
	else 
	    ret = PEC_LV_PE_DEV_NOT_FOUND;
	++mit;
	}
    y2mil( "pe_map:" << pe_map );
    y2mil( "ret:" << ret );
    return( ret );
    }

void PeContainer::addPv( const Pv* p )
    {
    list<Pv>::iterator i = find( pv.begin(), pv.end(), *p );
    if( i != pv.end() )
	*i = *p;
    else
	{
	i = find( pv_add.begin(), pv_add.end(), *p );
	if( i!=pv_add.end() )
	    pv_add.erase(i);
	pv.push_back( *p );
	}
    }


void 
PeContainer::init()
    {
    num_pe = free_pe = 0;
    pe_size = 1;
    }

unsigned long
PeContainer::leByLvRemove() const
    {
    unsigned long ret=0;
    ConstVolPair p=volPair(Volume::isDeleted);
    for( ConstVolIterator i=p.begin(); i!=p.end(); ++i )
	ret += static_cast<const Dm*>(&(*i))->getLe();
    y2milestone( "ret:%lu", ret );
    return( ret );
    }

bool
PeContainer::checkConsistency() const
    {
    bool ret = true;
    unsigned long sum = 0;
    ConstVolPair lp=volPair(Volume::notDeleted);
    map<string,unsigned long> peg;
    map<string,unsigned long>::iterator mi;
    for( ConstVolIterator l = lp.begin(); l!=lp.end(); ++l )
	{
	const Dm * dm = static_cast<const Dm*>(&(*l));
	ret = ret && dm->checkConsistency();
	map<string,unsigned long> pem = dm->getPeMap();
	for( map<string,unsigned long>::const_iterator mit=pem.begin();
	     mit!=pem.end(); ++mit )
	    {
	    if( (mi=peg.find( mit->first ))!=peg.end() )
		mi->second += mit->second;
	    else
		peg[mit->first] = mit->second;
	    }
	}
    sum = 0;
    list<Pv>::const_iterator p;
    for( map<string,unsigned long>::const_iterator mit=peg.begin();
	 mit!=peg.end(); ++mit )
	{
	sum += mit->second;
	if( (p=find( pv.begin(), pv.end(), mit->first ))!=pv.end()||
	    (p=find( pv_add.begin(), pv_add.end(), mit->first ))!=pv_add.end())
	    {
	    if( mit->second != p->num_pe-p->free_pe )
		{
		y2warning( "Vg:%s used pv %s is %lu should be %lu", 
		           name().c_str(), mit->first.c_str(),
		           mit->second,  p->num_pe-p->free_pe );
		ret = false;
		}
	    }
	else
	    {
	    y2warning( "Vg:%s pv %s not found", name().c_str(),
	               mit->first.c_str() );
	    ret = false;
	    }
	}
    if( sum != num_pe-free_pe )
	{
	y2warning( "Vg:%s used PE is %lu should be %lu", name().c_str(),
	           sum, num_pe-free_pe );
	ret = false;
	}
    return( ret );
    }
