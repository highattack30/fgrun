// wizard_funcs.cxx -- Wizard UI functions.
//
// Written by Bernie Bright, started Nov 2003.
//
// Copyright (c) 2003  Bernie Bright - bbright@users.sourceforge.net
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// $Id$

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string>
#include <cstdio>
#include <vector>
#include <sstream>

#include <FL/Fl.H>
#include <FL/Fl_Preferences.H>
#include <FL/filename.H>
#include <FL/fl_ask.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_draw.h>

#include <simgear/props/props_io.hxx>
#include <simgear/structure/exception.hxx>
#include <simgear/misc/sg_path.hxx>

#include <plib/ul.h>

#include "wizard.h"
#include "advanced.h"
#include "logwin.h"

using std::string;
using std::vector;

extern string def_fg_exe;
extern string def_fg_root;
extern string def_fg_scenery;


static bool
is_valid_fg_root( const string& dir )
{
    if (!fl_filename_isdir( dir.c_str() ))
        return false;

    string fname( dir );
    fname.append( "/version" );
    FILE* fp = fopen( fname.c_str(), "r" );
    if (fp != 0)
    {
	fclose(fp);
	return true;
    }

    return false;
}

void
Wizard::airports_cb( Fl_Widget*, void* v )
{
    ((Wizard*)v)->airports_cb();
}

void
Wizard::airports_cb()
{
    if (airports_->loaded())
    {
	airports_->show_installed();
	cache_delete_->activate();

	const int buflen = FL_PATH_MAX;
	char buf[ buflen ];

  	if (prefs.get( "airport", buf, "", buflen-1) && buf[0] != 0)
	{
	    airports_->select_id( buf );
	}
    }
}

static const char* about_text = "\
<html>\
 <head>\
  <title>FlightGear Launch Control " VERSION "</title>\
 </head>\
 <body>\
  <h1>FlightGear Launch Control " VERSION "</h1>\
  <p>This program is released under the GNU General Public License (http://www.gnu.org/copyleft/gpl.html).</p>\
  <p>Report bugs to bbright@users.sourceforge.net</p>\
 </body>\
</html>";

void
Wizard::init()
{
    static const int npages = 5;

    for (int i = 0; i < npages; ++i)
        page[i]->hide();

    logwin = new LogWindow( 640, 480, "Log Window" );

    const int buflen = FL_PATH_MAX;
    char buf[ buflen ];

    prefs.getUserdataPath( buf, sizeof(buf) );
    SGPath cache( buf );
    cache.append( "airports.txt" );
    cache_file_->value( cache.c_str() );

    about_->value( about_text );

    prefs.get( "fg_exe", buf, def_fg_exe.c_str(), buflen-1);
    fg_exe_->value( buf );

    prefs.get( "fg_root", buf, def_fg_root.c_str(), buflen-1);
    fg_root_->value( buf );

    string fg_scenery;
    if (!def_fg_scenery.empty())
    {
	fg_scenery = def_fg_scenery;
    }
    else if (prefs.get( "fg_scenery", buf, "", buflen-1))
    {
	fg_scenery = buf;
    }
    else if (fg_root_->size() > 0)
    {
	fg_scenery = fg_root_->value();
	fg_scenery += "/Scenery";
    }

    typedef vector<string> vs_t;
    vs_t v( sgPathSplit( fg_scenery ) );
    for (vs_t::size_type i = 0; i < v.size(); ++i)
	scenery_dir_list_->add( v[i].c_str() );

    if (fg_exe_->size() == 0 ||
	fg_root_->size() == 0 ||
        !is_valid_fg_root( fg_root_->value() ) ||
	fg_scenery.empty() )
    {
        // First time through or FG_ROOT is not valid.
        page[0]->activate();
        page[0]->show();
        prev->deactivate();
	next->deactivate();
    }
    else
    {
	SGPath path( fg_root_->value() );
	path.append( "/Airports/runways.dat.gz" );
	airports_->load_runways( path.str(), airports_cb, this );

	airports_->load_airports( v, cache, airports_cb, this );

        aircraft_update();

        prev->activate();
	next->activate();
        page[1]->show();
    }
}

void
Wizard::show()
{
    win->show();
}

void
Wizard::show( int ac, char* av[] )
{
    win->show( ac, av );
}

static const double update_period = 0.05;

static void
timeout_handler( void* v )
{
    ((Wizard*)v)->update_preview();
    Fl::repeat_timeout( update_period, timeout_handler, v );
}

static SGPath
dir_path( const SGPath& p )
{
    return SGPath( p.dir() );
}

void
Wizard::preview_aircraft()
{
    Fl::remove_timeout( timeout_handler, this );

    preview->clear();
    preview->redraw();
    preview->init();

    int n = aircraft->value();
    if (n == 0)
        return;

    string ac( aircraft->text(n) );
    string fname( (char*)aircraft->data(n) );

    SGPropertyNode props;
    try
    {
        readProperties( fname, &props );

        if (props.hasValue( "/sim/model/path" ))
        {
            SGPath path( fg_root_->value() );
            path.append( props.getStringValue( "/sim/model/path" ) );

            if (!path.exists())
	    {
		fl_alert( "Model not found: '%s'", path.c_str() );
                return;
	    }

	    if (path.extension() == "xml")
            {
                SGPropertyNode mprops;
                readProperties( path.str(), &mprops );

                if (mprops.hasValue( "/path" ))
                {
		    path = dir_path( path );
		    path.append( mprops.getStringValue("/path") );
                }
            }

            win->cursor( FL_CURSOR_WAIT );
	    Fl::flush();
            ssgEntity* model = preview->load( path.str() );
            if (model != 0)
            {
                Fl::add_timeout( update_period, timeout_handler, this );
            }
            win->cursor( FL_CURSOR_DEFAULT );
            preview->redraw();
        }
	else
	{
	    fl_alert( "Property '/sim/model/path' not found" );
	    return;
	}
    }
    catch (const sg_exception& exc )
    {
        fl_alert( exc.getFormattedMessage().c_str() );
        return;
    }

    next->activate();
}

#if defined(WIN32)
static const char searchPathSep = ';';
#else
static const char searchPathSep = ':';
#endif

void
Wizard::next_cb()
{
    prev->activate();

    if (wiz->value() == page[0])
    {
	char abs_name[ FL_PATH_MAX ];

	fl_filename_absolute( abs_name, fg_exe_->value() );
	prefs.set( "fg_exe", abs_name );

	fl_filename_absolute( abs_name, fg_root_->value() );
	prefs.set( "fg_root", abs_name );

	string fg_scenery = scenery_dir_list_->text(1);
	for (int i = 2; i <= scenery_dir_list_->size(); ++i)
	{
	    fg_scenery += searchPathSep;
	    fg_scenery += scenery_dir_list_->text(i);
	}

 	prefs.set( "fg_scenery", fg_scenery.c_str() );
    }
    else if (wiz->value() == page[1])
    {
	int n = aircraft->value();
	prefs.set( "aircraft", n > 0 ? aircraft->text(n) : "" );
	Fl::remove_timeout( timeout_handler, this );
    }
    else if (wiz->value() == page[2])
    {
	prefs.set( "airport", airports_->get_selected_id().c_str() );
	prefs.set( "airport-name",
		   airports_->get_selected_name().c_str() );

	string rwy( airports_->get_selected_runway() );
	if (rwy.empty())
	    rwy = "<default>";
	prefs.set( "runway", rwy.c_str() );
    }
    else if (wiz->value() == page[3])
    {
	prefs.flush();

	if (write_fgfsrc())
	{
	    run_fgfs();
	}
	return;
    }

    wiz->next();

    if (wiz->value() == page[1])
    {
	// "Select aircraft" page
        if (aircraft->size() == 0)
            aircraft_update();
    }
    else if (wiz->value() == page[2])
    {
	// "Select location" page
	if (!airports_->loaded())
	{
// 	    fl_cursor( FL_CURSOR_WAIT );
 	    win->cursor( FL_CURSOR_WAIT );
	    Fl::flush();
	}
    }
    else if (wiz->value() == page[3])
    {
	std::ostringstream ostr;
	ostr << fg_exe_->value() << "\n  ";
	write_fgfsrc( ostr, "\n  " );
	text->value( ostr.str().c_str() );
    }
}

void
Wizard::prev_cb()
{
    next->activate();
    wiz->prev();
    if (wiz->value() == page[0])
    {
        prev->deactivate();
    }
}

#if defined(WIN32) || defined(__EMX__) && !defined(__CYGWIN__)
static inline bool isdirsep(char c) {return c=='/' || c=='\\';}
#else
static inline bool isdirsep(char c) { return c =='/'; }
#endif

void
Wizard::fg_exe_select_cb()
{
    char* p = fl_file_chooser( "Select FlightGear executable",
                              fg_exe_->value(), 0);
    if (p != 0)
        fg_exe_->value( p );

    fg_exe_update_cb();
}

void
Wizard::fg_exe_update_cb()
{
//     if (fg_exe_->size() == 0)
// 	return;
}

void
Wizard::fg_root_update_cb()
{
    next->deactivate();

    if (fg_root_->size() == 0)
        return;

    string dir( fg_root_->value() );

    // Remove trailing separator.
    if (isdirsep( dir[ dir.length() - 1 ] ))
    {
        dir.erase( dir.length() - 1 );
    }

    if (!is_valid_fg_root( dir ))
    {
        dir.append( "/data" );
        if (!is_valid_fg_root( dir ))
            return;
    }

    fg_root_->value( dir.c_str() );

    if (scenery_dir_list_->size() == 0)
    {
	// Derive FG_SCENERY from FG_ROOT. 
	string d( dir );
	d.append( "/Scenery" );

	if (!fl_filename_isdir( d.c_str() ))
	    return;

	scenery_dir_list_->add( d.c_str() );
    }

    next->activate();
}

void
Wizard::fg_root_select_cb()
{
    char* p = fl_dir_chooser( "Select FG_ROOT directory",
                              fg_root_->value(), 0);
    if (p != 0)
        fg_root_->value( p );

    fg_root_update_cb();
}

void
Wizard::advanced_cb()
{
    static Advanced* adv = 0;

    if (adv == 0)
    {
	adv = new Advanced;
    }

    {
	prefs.set( "airport", airports_->get_selected_id().c_str() );
	prefs.set( "airport-name",
		   airports_->get_selected_name().c_str() );
    }

    int r = adv->exec( prefs );

    // Update command text.
    std::ostringstream ostr;
    ostr << fg_exe_->value() << "\n  ";
    write_fgfsrc( ostr, "\n  " );
    text->value( ostr.str().c_str() );
}

void
Wizard::update_preview()
{
    preview->update();
}

static void
search_aircraft_dir( const SGPath& dir,
                     bool recursive,
                     vector<SGPath>& ac )
{
    dirent** files;
    string s( dir.str() );

#ifdef WIN32
    // Ensure there is a trailing slash.
    if (*s.rbegin() != '/')
	s.append( "/" );
#endif

    int num_files = fl_filename_list( s.c_str(),
                                      &files, fl_casenumericsort );
    if (num_files < 0)
        return;

    for (int i = 0; i < num_files; ++i)
    {
        if (fl_filename_match(files[i]->d_name, "*-set.xml"))
        {
            SGPath d( dir );
            d.append( files[i]->d_name );
            ac.push_back( d );
        }
        else if (recursive &&
                 strcmp( files[i]->d_name, "CVS" ) != 0 &&
                 strcmp( files[i]->d_name, ".." ) != 0 &&
                 strcmp( files[i]->d_name, "." ) != 0 )
        {
            SGPath d( dir );
            d.append( files[i]->d_name );
            if (fl_filename_isdir( d.c_str() ))
            {
                search_aircraft_dir( d, false, ac );
            }
        }

        free( files[i] );
    }
    free( files );
}

void
Wizard::aircraft_update()
{
    SGPath path( fg_root_->value() );
    path.append( "Aircraft" );
    vector< SGPath > ac;
    search_aircraft_dir( path, true, ac );

    // Empty the aircraft browser list.
    for (int i = 1; i <= aircraft->size(); ++i)
    {
        char* data = (char*)aircraft->data(i);
        if (data != 0)
            free( data );
    }
    aircraft->clear();

    const int buflen = FL_PATH_MAX;
    char buf[ buflen ];
    prefs.get( "aircraft", buf, "", buflen-1);

    // Populate the aircraft browser list.
    for (vector<SGPath>::size_type vi = 0; vi < ac.size(); ++vi)
    {
        // Extract aircraft name from filename.
	string s( ac[vi].str() );
        string::size_type pos = s.rfind( "/" );
        string::size_type epos = s.find( "-set.xml", pos );

        char* data = strdup( s.c_str() );
	string ss( s.substr( pos+1, epos-pos-1 ) );
        aircraft->add( ss.c_str(), data );
	if (buf[0] != 0 && strcmp( buf, ss.c_str() ) == 0)
	{
	    aircraft->select( vi+1 );
	    //aircraft->do_callback();
	}
    }
}

Wizard::~Wizard()
{
    delete logwin;
    delete win;
}

void
Wizard::cancel_cb()
{
    logwin->hide();
    win->hide();
}

void
Wizard::delete_cache_file_cb()
{
    char buf[ FL_PATH_MAX ];
    prefs.getUserdataPath( buf, sizeof(buf) );
    SGPath path( buf );
    path.append( "/airports.txt" );

    if (!path.exists())
	return;

    //TODO: win32 support.
    if (unlink( path.c_str() ) == 0)
	return;

    fl_alert( "Unable to delete '%s':\n%s",
	      path.c_str(), strerror(errno) );
}

void
Wizard::scenery_dir_select_cb()
{
    scenery_dir_up_->deactivate();
    scenery_dir_down_->deactivate();
    int n = scenery_dir_list_->value();
    if (n > 0)
    {
	scenery_dir_delete_->activate();
	if (n > 1)
	    scenery_dir_up_->activate();
	if (n < scenery_dir_list_->size())
	    scenery_dir_down_->activate();

	scenery_dir_list_->deselect();
	scenery_dir_list_->select( n );
    }
    else
    {
	scenery_dir_delete_->deactivate();
    }
}

void
Wizard::scenery_dir_add_cb()
{
    char* p = fl_dir_chooser( "Select FG_SCENERY directory", 0, 0);
    if (p != 0)
    {
	scenery_dir_list_->add( p );
	scenery_dir_list_->value( scenery_dir_list_->size() );
	scenery_dir_delete_->activate();
	scenery_dir_list_->select( scenery_dir_list_->size() );
    }
}

void
Wizard::scenery_dir_delete_cb()
{
    int n = scenery_dir_list_->value();
    if (n > 0)
	scenery_dir_list_->remove( n );

    if (scenery_dir_list_->size() == 0)
	scenery_dir_delete_->deactivate();
}

/**
 * Move a directory entry up the list.
 */
void
Wizard::scenery_dir_up_cb()
{
    int from = scenery_dir_list_->value();
    int to = from - 1;
    scenery_dir_list_->move( to, from );

    scenery_dir_list_->deselect();
    scenery_dir_list_->select( to );

    scenery_dir_down_->activate();
    if (to == 1)
	scenery_dir_up_->deactivate();
}

/**
 * Move a directory entry down the list.
 */
void
Wizard::scenery_dir_down_cb()
{
    int n = scenery_dir_list_->value();
    scenery_dir_list_->insert( n+2, scenery_dir_list_->text(n) );
    scenery_dir_list_->remove( n );

    scenery_dir_list_->deselect();
    scenery_dir_list_->select( n+1 );

    scenery_dir_up_->activate();
    if (n+1 == scenery_dir_list_->size())
	scenery_dir_down_->deactivate();
}

