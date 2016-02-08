/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2013-2014 - Jason Fetters
 *  Copyright (C) 2011-2015 - Daniel De Matteis
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#import <objc/runtime.h>

#include <compat/apple_compat.h>
#include <retro_miscellaneous.h>

#include "cocoa_common.h"
#include "../../../menu/menu_setting.h"
#include "../../../input/drivers/cocoa_input.h"

#include "../../../driver.h"
#include "../../../input/input_common.h"
#include "../../../input/input_keymaps.h"

static void* const associated_name_tag = (void*)&associated_name_tag;

@interface RAInputBinder : NSWindow
{
#if !__OBJC2__
   NSTimer* _timer;
   const rarch_setting_t *_setting;
#endif
}

@property (nonatomic, retain) NSTimer* timer;
@property (nonatomic, assign) const rarch_setting_t* setting;
@end

@implementation RAInputBinder

@synthesize timer = _timer;
@synthesize setting = _setting;

- (void)dealloc
{
   [_timer release];
   [super dealloc];
}

- (void)runForSetting:(const rarch_setting_t*)setting onWindow:(NSWindow*)window
{
   self.setting = setting;
   self.timer = [NSTimer timerWithTimeInterval:.1f target:self selector:@selector(checkBind:) userInfo:nil repeats:YES];
   [[NSRunLoop currentRunLoop] addTimer:self.timer forMode:NSModalPanelRunLoopMode];
   
   [NSApp beginSheet:self modalForWindow:window modalDelegate:nil didEndSelector:nil contextInfo:nil];
}

- (IBAction)goAway:(id)sender
{
   [self.timer invalidate];
   self.timer = nil;
   
   [NSApp endSheet:self];
   [self orderOut:nil];
}

- (void)checkBind:(NSTimer*)send
{
   int32_t value = 0;
   int32_t idx   = 0;
   
   if (self.setting->index)
      idx = self.setting->index - 1;

   if ((value = cocoa_input_find_any_key()))
      BINDFOR(*[self setting]).key = input_keymaps_translate_keysym_to_rk(value);
   else if ((value = cocoa_input_find_any_button(idx)) >= 0)
      BINDFOR(*[self setting]).joykey = value;
   else if ((value = cocoa_input_find_any_axis(idx)))
      BINDFOR(*[self setting]).joyaxis = (value > 0) ? AXIS_POS(value - 1) : AXIS_NEG(abs(value) - 1);
   else
      return;

   [self goAway:self];
}

// Stop the annoying sound when pressing a key
- (void)keyDown:(NSEvent*)theEvent
{
}

@end


@interface RASettingsDelegate : NSObject
#ifdef MAC_OS_X_VERSION_10_6
<NSTableViewDataSource,   NSTableViewDelegate,
NSOutlineViewDataSource, NSOutlineViewDelegate,
NSWindowDelegate>
#endif
{
#if !__OBJC2__
   RAInputBinder* _binderWindow;
   NSButtonCell* _booleanCell;
   NSTextFieldCell* _binderCell;
   NSTableView* _table;
   NSOutlineView* _outline;
   NSMutableArray* _settings;
   NSMutableArray* _currentGroup;
#endif
}

@property (nonatomic, retain) RAInputBinder IBOutlet* binderWindow;
@property (nonatomic, retain) NSButtonCell IBOutlet* booleanCell;
@property (nonatomic, retain) NSTextFieldCell IBOutlet* binderCell;
@property (nonatomic, retain) NSTableView IBOutlet* table;
@property (nonatomic, retain) NSOutlineView IBOutlet* outline;
@property (nonatomic, retain) NSMutableArray* settings;
@property (nonatomic, retain) NSMutableArray* currentGroup;
@end

@implementation RASettingsDelegate

@synthesize binderWindow = _binderWindow;
@synthesize booleanCell = _booleanCell;
@synthesize binderCell = _binderCell;
@synthesize table = _table;
@synthesize outline = _outline;
@synthesize settings = _settings;
@synthesize currentGroup = _currentGroup;

- (void)dealloc
{
   [_binderWindow release];
   [_booleanCell release];
   [_binderCell release];
   [_table release];
   [_outline release];
   [_settings release];
   [_currentGroup release];
   
   [super dealloc];
}

- (void)awakeFromNib
{
   int i;
   NSMutableArray* thisGroup = nil;
   NSMutableArray* thisSubGroup = nil;
   driver_t *driver = driver_get_ptr();
   const rarch_setting_t *setting_data = (const rarch_setting_t *)driver->menu->list_settings;

   self.settings = [NSMutableArray array];

   for (i = 0; setting_data[i].type; i ++)
   {
      switch (setting_data[i].type)
      {
         case ST_GROUP:
         {
            thisGroup = [NSMutableArray array];
#if defined(MAC_OS_X_VERSION_10_6)
            /* FIXME - Rewrite this so that this is no longer an associated object - requires ObjC 2.0 runtime */
            objc_setAssociatedObject(thisGroup, associated_name_tag, BOXSTRING(setting_data[i].name), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#endif
            break;
         }
         
         case ST_END_GROUP:
         {
            if (thisGroup)
               [self.settings addObject:thisGroup];
            thisGroup = nil;
            break;
         }
         
         case ST_SUB_GROUP:
         {
            thisSubGroup = [NSMutableArray array];
#if defined(MAC_OS_X_VERSION_10_6)
            /* FIXME - Rewrite this so that this is no longer an associated object - requires ObjC 2.0 runtime */
            objc_setAssociatedObject(thisSubGroup, associated_name_tag, BOXSTRING(setting_data[i].name), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
#endif
            break;
         }
         
         case ST_END_SUB_GROUP:
         {
            if (thisSubGroup)
               [thisGroup addObject:thisSubGroup];
            thisSubGroup = nil;
            break;
         }

         default:
         {
            [thisSubGroup addObject:[NSNumber numberWithInt:i]];
            break;
         }
      }
   }
}

- (void)windowWillClose:(NSNotification *)notification
{
   [NSApp stopModal];
}

#pragma mark Section Table
- (NSInteger)numberOfRowsInTableView:(NSTableView*)view
{
   return self.settings.count;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
#if defined(MAC_OS_X_VERSION_10_6)
   return objc_getAssociatedObject([self.settings objectAtIndex:row], associated_name_tag);
#else
	/* FIXME - Rewrite this so that this is no longer an associated object - requires ObjC 2.0 runtime */
	return 0; /* stub */
#endif
}

- (void)tableViewSelectionDidChange:(NSNotification *)aNotification
{
   self.currentGroup = [self.settings objectAtIndex:[self.table selectedRow]];
   [self.outline reloadData];
}

#pragma mark Setting Outline
- (NSInteger)outlineView:(NSOutlineView *)outlineView numberOfChildrenOfItem:(id)item
{
   return (item == nil) ? [self.currentGroup count] : [item count];
}

- (id)outlineView:(NSOutlineView *)outlineView child:(NSInteger)idx ofItem:(id)item
{
   return (item == nil) ? [self.currentGroup objectAtIndex:idx] : [item objectAtIndex:idx];
}

- (BOOL)outlineView:(NSOutlineView *)outlineView isItemExpandable:(id)item
{
   return [item isKindOfClass:[NSArray class]];
}

- (BOOL)validateProposedFirstResponder:(NSResponder*)responder forEvent:(NSEvent*)event
{
    return YES;
}

- (id)outlineView:(NSOutlineView *)outlineView objectValueForTableColumn:(NSTableColumn *)tableColumn byItem:(id)item
{
   driver_t *driver = driver_get_ptr();
    
   if (!tableColumn)
      return nil;

   if ([item isKindOfClass:[NSArray class]])
   {
#ifdef MAC_OS_X_VERSION_10_6
	  /* FIXME - Rewrite this so that this is no longer an associated object - requires ObjC 2.0 runtime */
      if ([[tableColumn identifier] isEqualToString:BOXSTRING("left")])
         return objc_getAssociatedObject(item, associated_name_tag);
#endif
      return BOXSTRING("");
   }
   else
   {
      char buffer[PATH_MAX_LENGTH];
      rarch_setting_t *setting_data = (rarch_setting_t*)driver->menu->list_settings;
      rarch_setting_t *setting = (rarch_setting_t*)&setting_data[[item intValue]];

      if ([[tableColumn identifier] isEqualToString:BOXSTRING("left")])
         return BOXSTRING(setting->short_description);

      switch (setting->type)
      {
         case ST_BOOL:
            return BOXINT(*setting->value.boolean);
         default:
            {
               setting_get_string_representation(setting, buffer, sizeof(buffer));
               if (buffer[0] == '\0')
                  strlcpy(buffer, "N/A", sizeof(buffer));
               return BOXSTRING(buffer);
            }
      }
   }
}

- (NSCell*)outlineView:(NSOutlineView *)outlineView dataCellForTableColumn:(NSTableColumn *)tableColumn item:(id)item
{
   const rarch_setting_t *setting_data, *setting;
   driver_t *driver = driver_get_ptr();
    
   if (!tableColumn)
      return nil;
   
   if ([item isKindOfClass:[NSArray class]])
      return [tableColumn dataCell];
   
   if ([[tableColumn identifier] isEqualToString:BOXSTRING("left")])
      return [tableColumn dataCell];

   setting_data = (const rarch_setting_t *)driver->menu->list_settings;
   setting      = (const rarch_setting_t *)&setting_data[[item intValue]];

   switch (setting->type)
   {
      case ST_BOOL:
           return self.booleanCell;
      case ST_BIND:
           return self.binderCell;
      default:
           break;
   }

   return tableColumn.dataCell;
}

- (IBAction)outlineViewClicked:(id)sender
{
   id item;
   driver_t *driver = driver_get_ptr();
    
   if ([self.outline clickedColumn] != 1)
      return;
   
   item = [self.outline itemAtRow:[self.outline clickedRow]];
      
   if (![item isKindOfClass:[NSNumber class]])
      return;
   
      {
          rarch_setting_t *setting_data = (rarch_setting_t*)driver->menu->list_settings;
          rarch_setting_t *setting      = (rarch_setting_t*)&setting_data[[item intValue]];
          
          switch (setting->type)
          {
            case ST_BOOL:
                 *setting->value.boolean = !*setting->value.boolean;
                 break;
            case ST_BIND:
                 [self.binderWindow runForSetting:setting onWindow:[self.outline window]];
                 break;
             default:
                 break;
          }
          
          if (setting->change_handler)
              setting->change_handler(setting);
      }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
   id item;
   NSText* editor = NULL;
   driver_t *driver = driver_get_ptr();

   if ([notification object] != self.outline)
      return;

   editor = [[notification userInfo] objectForKey:BOXSTRING("NSFieldEditor")];
   item        = [self.outline itemAtRow:[self.outline selectedRow]];

   if (![item isKindOfClass:[NSNumber class]])
      return;
   
   {
      rarch_setting_t *setting_data = (rarch_setting_t *)driver->menu->list_settings;
      rarch_setting_t *setting      = (rarch_setting_t*)&setting_data[[item intValue]];
      NSString *editor_string       = (NSString*)editor.string;

      setting_set_with_string_representation(setting, editor_string.UTF8String);
   }
}

@end
