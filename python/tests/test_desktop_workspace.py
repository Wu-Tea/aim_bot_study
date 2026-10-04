"""Independent profiles and observable native-widget interaction contracts."""
from copy import deepcopy
import json
import gc
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import threading
import tkinter as tk
from tkinter import ttk
import tomllib
import unittest
from unittest.mock import patch

from desktop_app.components import ChoiceInput, ScrollSurface, StrengthInput, configure_theme
from desktop_app.curve_model import CurveModel
from desktop_app.curves import curve_document, decode_points, encode_points, validate_points
from desktop_app.gui import AssistantWindow
from desktop_app.runtime import RuntimeManager
from desktop_app.settings import effective, lookup
from desktop_app.workspace import ProfileRepository, projection, snapshot, toml_text

PROJECT=Path(__file__).resolve().parents[2]
PRODUCTION=PROJECT/'native/build/Release/cod_native_runtime.exe'


class IndependentStorageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        self.repo=ProfileRepository(self.root,PROJECT)
    def tearDown(self):self.temp.cleanup()

    def test_empty_library_does_not_seed_or_rewrite_history(self):
        path=self.root/'config.toml';path.write_text('historical user bytes',encoding='utf-8')
        self.assertEqual(self.repo.entries(),[])
        self.assertEqual(path.read_text(encoding='utf-8'),'historical user bytes')

    def test_defaults_independence_rename_and_apex_linear_reopen(self):
        a=self.repo.create('Apex 日常','apex');b=self.repo.duplicate(a,'Apex 比赛')
        saved,expected=self.repo.read(self.repo.path(a['id']))
        saved['name']='Apex 训练';saved['config']['gamepad']['ads']['strength_scale']=.735
        self.repo.save(saved,expected)
        reopened=self.repo.read(self.repo.path(a['id']))[0]
        self.assertEqual(reopened['curve']['algorithm'],'linear')
        self.assertEqual(reopened['id'],a['id'])
        self.assertEqual(lookup(self.repo.read(self.repo.path(b['id']))[0]['config'],'gamepad.ads.strength_scale'),1.)
        self.assertTrue(list((self.root/'runs/desktop/profile-backups').glob('*.json')))

    def test_flat_apex_profile_has_no_inheritance_and_native_consumes_custom_curve(self):
        p=self.repo.create('曲线','apex')
        _,expected=self.repo.read(self.repo.path(p['id']))
        points=[[0,0],[.2,.08],[.5,.37],[1,1]]
        p['curve']={'algorithm':'custom_lut','definition':curve_document('精调',points)}
        self.repo.save(p,expected,lambda path:RuntimeManager(PROJECT).validate(path,['apex']))
        doc=tomllib.loads(self.repo.runtime_path(p).read_text(encoding='utf-8'))
        self.assertNotIn('games',doc)
        self.assertEqual(effective(doc,'apex')['gamepad']['aim_response_curve']['custom_points'],encode_points(points))
        result=subprocess.run([str(PRODUCTION),'--config',str(self.repo.runtime_path(p)),'--game','apex','--dump-effective-config'],
            capture_output=True,timeout=15,creationflags=0x08000000)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('gamepad.aim_response_curve.algorithm=custom_lut',result.stdout.decode())
        with self.assertRaises(ValueError):effective(doc,'bo3')

    def test_import_flattens_selected_override_and_preserves_source_and_hidden_fields(self):
        text='[runtime]\ngame="default"\n[gamepad.ads]\nstrength_scale=.8\n[games.apex.gamepad.ads]\nstrength_scale=.6\n'
        # TOML requires leading zero on floats.
        text=text.replace('=.','=0.')
        path=self.root/'import.toml';path.write_text(text,encoding='utf-8')
        p=self.repo.import_file(path,'导入','apex')
        self.assertEqual(lookup(p['config'],'gamepad.ads.strength_scale'),.6)
        self.assertNotIn('games',p['config'])
        self.assertEqual(path.read_text(encoding='utf-8'),text)
        source=tomllib.loads(text);before=deepcopy(source)
        snapshot(source,'bo3',PROJECT)
        self.assertEqual(source,before)
        common={'runtime':{'game':'default'},'gamepad':{'ads':{'strength_scale':.9}}}
        self.assertEqual(lookup(snapshot(common,'apex',PROJECT)['config'],'gamepad.ads.strength_scale'),.9)

    def test_bad_import_types_are_not_silently_coerced(self):
        with self.assertRaises(ValueError):self.repo.create('坏配置','apex',source={'gamepad':{'recoil':{'enabled':'false'}}})
        self.assertEqual(self.repo.entries(),[])

    def test_rejected_validation_and_external_changes_do_not_overwrite_user_data(self):
        def reject(_):raise ValueError('NATIVE_REJECT')
        with self.assertRaisesRegex(ValueError,'NATIVE_REJECT'):self.repo.create('拒绝','apex',validate=reject)
        self.assertEqual(list(self.repo.folder.glob('*')),[])
        p=self.repo.create('配置','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        self.repo.path(p['id']).write_bytes(expected+b' ')
        with self.assertRaisesRegex(ValueError,'外部修改'):self.repo.save(p,expected)
        self.assertEqual(self.repo.path(p['id']).read_bytes(),expected+b' ')

    def test_modified_projection_is_reported_instead_of_overwritten(self):
        p=self.repo.create('配置','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        path=self.repo.runtime_path(p);path.write_text('# user change\n',encoding='utf-8')
        with self.assertRaisesRegex(ValueError,'外部修改'):self.repo.save(p,expected)
        self.assertEqual(path.read_text(encoding='utf-8'),'# user change\n')

    def test_corrupt_entry_does_not_hide_valid_profiles(self):
        p=self.repo.create('配置','apex')
        (self.repo.folder/('p_'+'f'*32+'.json')).write_text('{}',encoding='utf-8')
        self.assertEqual([e['id'] for e in self.repo.entries()],[p['id']])
        self.assertEqual(len(self.repo.errors),1)

    def test_close_precision_points_are_accepted_by_native_loader(self):
        p=self.repo.create('精校','apex');_,expected=self.repo.read(self.repo.path(p['id']))
        points=[[0,0],[.09999512344,.2],[.10000612344,.4],[1,1]]
        p['curve']={'algorithm':'custom_lut','definition':curve_document('精校',points)}
        self.repo.save(p,expected,lambda path:RuntimeManager(PROJECT).validate(path,['apex']))

    def test_json_import_keeps_game_curve_and_immutable_baseline(self):
        p=self.repo.create('配置','apex')
        q=self.repo.import_file(self.repo.path(p['id']),'副本','default')
        self.assertEqual(q['game'],'apex')
        self.assertNotEqual(q['id'],p['id'])
        self.assertEqual(q['initial']['curve'],p['curve'])


class CurveTransactionTests(unittest.TestCase):
    def test_precision_points_survive_serialization_at_decimal_magnitude_boundary(self):
        points=[[0,0],[.09999512344,.2],[.10000612344,.4],[1,1]]
        self.assertEqual(decode_points(encode_points(points)),validate_points(points))

    def test_drag_is_one_undo_transaction_with_crossing_and_endpoint_constraints(self):
        initial=[[0,0],[.2,.1],[.5,.4],[1,1]]
        model=CurveModel(initial);model.begin()
        for x,y in [(.3,.2),(.6,.9),(.9,1.2)]:model.drag(1,x,y)
        model.commit();validate_points(model.points)
        self.assertEqual(len(model.undo_stack),1)
        self.assertLess(model.points[1][0],.5);self.assertLess(model.points[1][1],.4)
        model.undo();self.assertEqual(model.points,initial)
        model.redo();self.assertNotEqual(model.points,initial)
        model.drag(0,.2,.3);self.assertEqual(model.points[0],[0,0])

    def test_precision_rejects_crossing_without_clamping_and_supports_point_lifecycle(self):
        model=CurveModel([[0,0],[.5,.5],[1,1]])
        with self.assertRaises(ValueError):model.exact(1,.6,1.2)
        self.assertEqual(model.points[1],[.5,.5])
        model.exact(1,.513456,.428765);self.assertEqual(model.points[1],[.513456,.428765])
        model.add(.75,.8);model.remove();model.undo()
        self.assertEqual(len(model.points),4)


class DesktopWorkspaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host=tk.Tk();cls.host.withdraw()
    @classmethod
    def tearDownClass(cls):cls.host.destroy()
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.project=Path(self.temp.name)
        header=self.project/'native/controller_native/aim_response_curve_plugin.h';header.parent.mkdir(parents=True)
        shutil.copyfile(PROJECT/'native/controller_native/aim_response_curve_plugin.h',header)
        self.repo=ProfileRepository(self.project)
        self.a=self.repo.create('Apex 日常','apex');self.b=self.repo.duplicate(self.a,'Apex 比赛')
        self.root=tk.Toplevel(self.host)
        self.app=AssistantWindow(self.root,self.project)
        self.app.select_profile(self.a['id'])
        self.root.geometry('900x740+20+20');self.root.update()
        self.app.manager.executable=PRODUCTION
    def tearDown(self):
        if self.root.winfo_exists():self.app.closed=True;self.root.after_cancel(self.app.poll_id);self.root.destroy()
        self.temp.cleanup()
    def finish(self):
        deadline=time.monotonic()+10
        while self.app.busy:
            self.app.process_jobs()
            self.root.update()
            if time.monotonic()>deadline:self.fail('background operation did not finish')
            time.sleep(.01)

    def test_dense_parameter_page_has_twelve_visible_fields_at_900_by_740(self):
        self.assertEqual(len(self.app.field_widgets),12)
        bottom=self.app.surface.canvas.winfo_rooty()+self.app.surface.canvas.winfo_height()
        for widget,_ in self.app.field_widgets.values():
            self.assertLessEqual(widget.winfo_rooty()+widget.winfo_height(),bottom)
        self.assertEqual(len(self.app.navigation),4)
        self.assertFalse(hasattr(self.app,'game'))

    def test_navigation_and_profile_switch_reuse_parameter_controls(self):
        surface=self.app.surface
        strength=self.app.field_widgets['gamepad.ads.strength_scale'][0]
        self.app.variables['gamepad.ads.strength_scale'].set('bad')
        self.app.show_page('device');self.app.show_page('assist')
        self.assertIs(self.app.surface,surface)
        self.assertIs(self.app.field_widgets['gamepad.ads.strength_scale'][0],strength)
        self.app.select_profile(self.b['id']);self.app.select_profile(self.a['id'])
        self.assertIs(self.app.field_widgets['gamepad.ads.strength_scale'][0],strength)
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'bad')
        self.assertTrue(strength.entry.instate(['invalid']))

    def test_parameter_and_curve_edits_do_not_probe_process_or_redraw_unchanged_strip(self):
        with patch.object(self.app.manager,'active') as active:
            self.app.variables['gamepad.ads.strength_scale'].set('.71')
            with patch.object(self.app.profile_strip,'draw') as draw:
                self.app.variables['gamepad.ads.strength_scale'].set('.72')
                draw.assert_not_called()
            active.assert_not_called()
        self.app.show_page('curve');self.root.update()
        with patch.object(self.app.manager,'active') as active:
            self.app.curve_editor.move_point(3,.31,.27)
            active.assert_not_called()

    def test_curve_drag_reuses_canvas_items_and_skips_hidden_table(self):
        self.app.show_page('curve');self.root.update()
        editor=self.app.curve_editor
        items=editor.find_all()
        with patch.object(self.app.point_table,'item') as table_update:
            editor.move_point(3,.31,.27)
            self.assertEqual(editor.find_all(),items)
            table_update.assert_not_called()

    def test_retained_pages_advanced_groups_and_point_table_keep_current_profile(self):
        self.app.advanced.set(True);self.app.show_page('assist')
        group=self.app.advanced_group
        self.app.advanced.set(False);self.app.show_page('assist')
        self.assertFalse(group.winfo_manager())
        self.app.advanced.set(True);self.app.show_page('assist')
        self.assertIs(self.app.advanced_group,group)
        self.app.show_page('curve');self.root.update()
        self.app.curve_editor.move_point(3,.32,.27)
        self.app.show_point_table.set(True);self.app.show_page('curve');self.root.update()
        self.assertEqual(self.app.point_table.item('3','values'),('32','27'))
        self.app.select_profile(self.b['id']);self.root.update()
        self.assertEqual(self.app.point_table.item('3','values'),('30','30'))
        self.app.show_point_table.set(False);self.app.show_page('curve')
        selected=self.app.curve_editor.selected
        self.app.point_table.selection_set('8');self.root.update()
        self.assertEqual(self.app.curve_editor.selected,selected)
        for _ in range(3):
            for page in ('assist','device','feedback','curve'):self.app.show_page(page)
            self.app.select_profile(self.a['id']);self.app.select_profile(self.b['id'])
        self.assertEqual(len(self.app.content_host.winfo_children()),4)
        self.assertEqual(set(self.app.views),{'assist','device','feedback','curve'})

    def test_long_native_error_does_not_collapse_parameter_workspace(self):
        before=self.app.surface.canvas.winfo_height()
        self.app.notice.set('配置校验失败\n'+('\n'.join('未知字段 '+str(i) for i in range(40))))
        self.root.update()
        self.assertEqual(self.app.surface.canvas.winfo_height(),before)
        self.assertTrue(self.app.notice_detail.winfo_ismapped())

    def test_new_profile_uses_real_native_defaults_and_requires_explicit_model_choice(self):
        with patch.object(self.app,'name_dialog',return_value=('新建 Apex','apex')):
            self.app.new_profile();self.finish()
        self.assertEqual(self.app.profile['name'],'新建 Apex',self.app.notice.get())
        self.assertEqual(self.app.profile['curve']['algorithm'],'linear')
        self.assertEqual(self.app.variables['runtime.profile'].get(),'legacy')
        self.assertEqual(self.app.variables['gamepad.bodylock.strength'].get(),'0.3')
        self.assertEqual(self.app.variables['runtime.vision.capture_fps'].get(),'140')
        self.assertEqual(self.app.variables['runtime.vision.model_path'].get(),'')

    def test_missing_model_keeps_running_instance_and_tracks_successful_save(self):
        record={'game':'apex','config_path':str(self.repo.runtime_path(self.b)),'process_id':44}
        with patch.object(self.app.manager,'active',return_value=record),patch.object(self.app.manager,'stop') as stop:
            self.app.variables['gamepad.ads.strength_scale'].set('.75')
            self.app.primary_action();self.finish()
            stop.assert_not_called()
            self.assertFalse(self.app.dirty())
            self.assertIn('模型文件不存在',self.app.notice.get())
            self.assertEqual(self.app.state['expected'],self.repo.path(self.a['id']).read_bytes())

    def test_escape_cancels_whole_drag_and_restores_builtin_algorithm(self):
        self.app.show_page('curve');self.root.update();editor=self.app.curve_editor
        original=deepcopy(self.app.profile['curve']);x,y=editor.pixel(*editor.points[3])
        editor.event_generate('<Button-1>',x=int(x),y=int(y));editor.event_generate('<B1-Motion>',x=int(x+5),y=int(y+5))
        editor.cancel();self.assertEqual(self.app.profile['curve'],original)
        self.assertEqual(editor.model.undo_stack,[])

    def test_parameter_context_restores_only_selected_creation_value_as_draft(self):
        self.app.variables['gamepad.ads.strength_scale'].set('.65')
        self.app.variables['gamepad.recoil.feedback_amount'].set('.3')
        widget,_=self.app.field_widgets['gamepad.ads.strength_scale']
        widget.entry.event_generate('<Button-3>',x=4,y=4);self.root.update()
        self.app.field_popup.choose('initial')
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'1.0')
        self.assertEqual(self.app.variables['gamepad.recoil.feedback_amount'].get(),'.3')
        self.assertTrue(self.app.dirty())

    def test_search_reveals_matching_advanced_fields_without_losing_raw_values(self):
        self.app.variables['gamepad.ads.strength_scale'].set('.745')
        self.app.search.set('ads_free');self.root.update()
        self.assertIn('gamepad.ai_aim.ads_free_initial_scale',self.app.field_widgets)
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'.745')

    def test_curve_preset_export_import_and_rebuild_keep_only_live_controls(self):
        self.app.show_page('curve');self.root.update()
        self.app.curve_editor.move_point(3,.32,.27)
        path=self.project/'curve-export.json'
        with patch('desktop_app.gui.filedialog.asksaveasfilename',return_value=str(path)):self.app.export_curve()
        document=json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(document['schema_version'],1)
        self.app.curve_choice.set('linear')
        with patch('desktop_app.gui.filedialog.askopenfilename',return_value=str(path)):self.app.import_curve()
        self.assertEqual(self.app.curve_editor.points,document['points'])
        with patch.object(self.app,'name_dialog',return_value='训练曲线'):self.app.save_curve_preset()
        self.assertTrue(all(w.winfo_exists() for w in self.app.inputs))
        self.assertEqual(len(self.app.curve_library.entries()),1)
        self.app.run_job('校验',lambda:None);self.finish()

    def test_profile_export_copy_and_rename_preserve_identity_and_isolation(self):
        self.app.variables['gamepad.ads.strength_scale'].set('.7321')
        exported=self.project/'exported.json'
        with patch('desktop_app.gui.filedialog.asksaveasfilename',return_value=str(exported)):
            self.app.export_profile();self.finish()
        self.assertEqual(lookup(json.loads(exported.read_text(encoding='utf-8'))['config'],'gamepad.ads.strength_scale'),.7321)
        with patch.object(self.app,'name_dialog',return_value='副本'):
            self.app.manage_value.set('copy');self.finish()
        copied=self.app.profile['id']
        self.assertNotEqual(copied,self.a['id'])
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'0.7321')
        with patch.object(self.app,'name_dialog',return_value='比赛配置'):
            self.app.manage_value.set('rename');self.finish()
        self.assertEqual(self.app.profile['id'],copied)
        self.assertEqual(self.app.profile['name'],'比赛配置')
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.strength_scale'),1.)

    def test_full_editor_changes_draft_and_rejects_cross_game_source(self):
        from desktop_app.settings import update_text
        self.app.raw_editor();self.root.update()
        window=next(w for w in self.root.winfo_children() if isinstance(w,tk.Toplevel))
        frame=window.winfo_children()[0]
        editor=next(w for w in frame.winfo_children() if isinstance(w,tk.Text))
        button=next(w for w in frame.winfo_children() if isinstance(w,ttk.Button))
        original=editor.get('1.0','end-1c')
        editor.delete('1.0','end');editor.insert('1.0',update_text(original,{'runtime.game':'bo3'}));button.invoke()
        self.assertTrue(window.winfo_exists())
        editor.delete('1.0','end');editor.insert('1.0',update_text(original,{'gamepad.ads.strength_scale':.74567}));button.invoke()
        self.assertFalse(window.winfo_exists());self.assertTrue(self.app.dirty())
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'0.74567')
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.strength_scale'),1.)

    def test_shared_profile_variable_traces_remain_bounded_and_write_only_active_draft(self):
        variable=self.app.variables['gamepad.ads.strength_scale']
        traces=variable.trace_info()
        for _ in range(8):
            self.app.select_profile(self.a['id']);self.app.select_profile(self.b['id'])
        self.assertIs(variable,self.app.variables['gamepad.ads.strength_scale'])
        self.assertEqual(variable.trace_info(),traces)
        variable.set('.77')
        self.assertEqual(self.app.states[self.a['id']]['raw']['gamepad.ads.strength_scale'],'1.0')
        self.assertEqual(self.app.states[self.b['id']]['raw']['gamepad.ads.strength_scale'],'.77')

    def test_learning_export_keeps_raw_estimates_and_fusion_remains_independent(self):
        data={'revision':3,'regions':[{'effective':123.456,'learned':0.,'confidence':.1,'samples':0}]}
        with patch.object(self.app.manager,'learning',return_value=data),patch.object(self.app.manager,'active',return_value=None):
            self.app.export_learning()
        path,=(self.project/'runs/desktop/learning').glob('*.json')
        exported=json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(exported['learning'],data)
        self.assertIn('not independent',exported['measurement_kind'])
        with patch.object(self.app.manager,'fusion_state',return_value=True),patch.object(self.app.manager,'set_fusion') as fusion,patch.object(self.app.manager,'stop') as stop:
            self.app.toggle_fusion();self.finish();fusion.assert_called_once_with(False);stop.assert_not_called()

    def test_navigation_and_profile_switch_keep_invalid_and_valid_drafts_independent(self):
        self.app.variables['gamepad.ads.strength_scale'].set('.735')
        self.app.variables['gamepad.recoil.feedback_amount'].set('bad')
        self.app.show_page('device');self.app.show_page('assist')
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'.735')
        self.app.select_profile(self.b['id']);self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'1.0')
        self.app.select_profile(self.a['id']);self.assertEqual(self.app.variables['gamepad.recoil.feedback_amount'].get(),'bad')
        with self.assertRaises(ValueError):self.app.collect()
        self.assertIn('未保存',self.app.change_summary.get())

    def test_save_uses_production_loader_and_reopen_restores_profile_and_curve(self):
        self.app.variables['gamepad.ads.strength_scale'].set('.735')
        self.app.save();self.finish();self.assertFalse(self.app.dirty(),self.app.notice.get())
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.strength_scale'),.735)
        self.app.closed=True;self.root.after_cancel(self.app.poll_id);self.root.destroy()
        self.root=tk.Toplevel(self.host);self.app=AssistantWindow(self.root,self.project);self.root.update()
        self.assertEqual(self.app.profile['id'],self.a['id'])
        self.assertEqual(self.app.profile['curve']['algorithm'],'linear')
        self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),'0.735')

    def test_slider_preserves_typed_precision_and_invalid_text(self):
        widget,_=self.app.field_widgets['gamepad.ads.strength_scale']
        variable=self.app.variables['gamepad.ads.strength_scale']
        variable.set('0.734567');self.assertEqual(variable.get(),'0.734567')
        variable.set('bad');self.assertEqual(variable.get(),'bad')
        self.assertTrue(widget.entry.instate(['invalid']))

    def test_wheel_over_choice_slider_and_entry_scrolls_and_never_changes_values_or_focus(self):
        surface=self.app.surface
        ttk.Frame(surface.content,height=1200).pack()
        self.root.update()
        value=self.app.variables['gamepad.auto_fire.fire_output']
        widget,_=self.app.field_widgets['gamepad.auto_fire.fire_output']
        focus=self.root.focus_get();before=value.get()
        widget.event_generate('<Enter>');widget.event_generate('<MouseWheel>',delta=-120);self.root.update()
        self.assertEqual(value.get(),before);self.assertEqual(self.root.focus_get(),focus)
        self.assertGreater(surface.canvas.yview()[0],0)
        strength,_=self.app.field_widgets['gamepad.ads.strength_scale'];old=self.app.variables['gamepad.ads.strength_scale'].get()
        strength.scale.event_generate('<MouseWheel>',delta=-120);strength.entry.event_generate('<MouseWheel>',delta=-120)
        self.root.update();self.assertEqual(self.app.variables['gamepad.ads.strength_scale'].get(),old)

    def test_choice_popup_hover_wheel_escape_and_keyboard_commit(self):
        widget,_=self.app.field_widgets['gamepad.auto_fire.fire_output']
        old=self.app.variables['gamepad.auto_fire.fire_output'].get()
        widget.open();self.root.update();widget.popup.focus_force();self.root.update()
        widget.menu_rows[0].event_generate('<Enter>');widget.popup.event_generate('<MouseWheel>',delta=120)
        self.assertEqual(self.app.variables['gamepad.auto_fire.fire_output'].get(),old)
        widget.popup.event_generate('<Escape>');self.root.update();self.assertIsNone(widget.popup)
        widget.open();self.root.update();widget.popup.focus_force();self.root.update()
        widget.highlight(0);widget.popup.event_generate('<Return>');self.root.update()
        self.assertEqual(self.app.variables['gamepad.auto_fire.fire_output'].get(),'RT')

    def test_curve_drag_precision_history_persistence_and_native_projection(self):
        self.app.show_page('curve');self.root.update()
        editor=self.app.curve_editor;x,y=editor.pixel(*editor.points[3])
        editor.event_generate('<Button-1>',x=int(x),y=int(y))
        for dx in (2,4,6):editor.event_generate('<B1-Motion>',x=int(x+dx),y=int(y+8))
        editor.event_generate('<ButtonRelease-1>',x=int(x+6),y=int(y+8))
        self.assertEqual(len(editor.model.undo_stack),1)
        self.assertEqual(self.app.profile['curve']['algorithm'],'custom_lut')
        editor.select(3);self.app.point_x.set('32.3456');self.app.point_y.set('27.4567')
        self.assertTrue(self.app.apply_precision())
        self.assertAlmostEqual(editor.points[3][0],.323456)
        editor.undo();self.assertNotAlmostEqual(editor.points[3][0],.323456)
        editor.redo();self.assertAlmostEqual(editor.points[3][0],.323456)
        self.app.save();self.finish();self.assertFalse(self.app.dirty(),self.app.notice.get())
        saved=self.repo.read(self.repo.path(self.a['id']))[0]
        self.assertEqual(saved['curve']['definition']['points'],editor.points)
        self.assertEqual(lookup(projection(saved),'gamepad.aim_response_curve.custom_points'),encode_points(editor.points))

    def test_invalid_precision_blocks_save_without_changing_curve_or_file(self):
        self.app.show_page('curve');self.root.update();editor=self.app.curve_editor
        before=deepcopy(editor.points);original=self.repo.path(self.a['id']).read_bytes()
        self.app.point_y.set('NaN');self.app.save()
        self.assertFalse(self.app.busy);self.assertEqual(editor.points,before)
        self.assertEqual(self.repo.path(self.a['id']).read_bytes(),original)
        self.assertTrue(self.app.point_error.get())

    def test_curve_undo_survives_page_switch_and_busy_editor_cannot_edit(self):
        self.app.show_page('curve');self.root.update()
        self.app.curve_editor.move_point(3,.32,.27)
        self.app.show_page('device');self.app.show_page('curve');self.root.update()
        self.assertEqual(len(self.app.curve_editor.model.undo_stack),1)
        before=deepcopy(self.app.curve_editor.points)
        self.app.run_job('校验',lambda:None)
        self.app.curve_editor.move_point(3,.31,.28);self.assertEqual(self.app.curve_editor.points,before)
        self.finish();self.app.curve_editor.undo();self.assertEqual(self.app.curve_editor.points[3],[.3,.3])
        self.assertEqual(self.app.profile['curve']['algorithm'],'linear')
        self.assertFalse(self.app.dirty())

    def test_runtime_ownership_uses_profile_path_and_start_never_launches_fusion(self):
        model=self.project/'model.engine';model.write_bytes(b'test placeholder; runtime start is mocked')
        self.app.variables['runtime.vision.model_path'].set('model.engine')
        a={'game':'apex','config_path':str(self.repo.runtime_path(self.a))}
        b={'game':'apex','config_path':str(self.repo.runtime_path(self.b))}
        self.assertTrue(self.app.owns_active_config(a));self.assertFalse(self.app.owns_active_config(b))
        with (patch.object(self.app.manager,'active',return_value=None),patch.object(self.app.manager,'start',return_value={'process_id':42}) as start,
              patch.object(self.app.manager,'set_fusion') as fusion):
            self.app.primary_action();self.finish()
            self.assertEqual(start.call_args.args[0],'apex');self.assertEqual(start.call_args.args[3],self.a['id'])
            fusion.assert_not_called()

    def test_pending_reload_must_match_pid_and_request_and_restart_is_truthful(self):
        with patch.object(self.app.manager,'active',return_value={'process_id':42}):
            self.app.applied({'status':1,'request_id':23})
        self.assertEqual(self.app.pending_reload,(42,23));self.assertIn('尚未全部生效',self.app.notice.get())
        self.app.applied({'status':3,'message':'restart required: model'})
        self.assertTrue(self.app.restart_required);self.assertIn('需要重启',self.app.notice.get())
        self.app.applied({'status':2,'revision':5,'message':'learning preserved'})
        self.assertFalse(self.app.restart_required);self.assertIn('保留',self.app.notice.get())

    def test_poll_ignores_other_reload_requests_before_accepting_matching_acknowledgement(self):
        record={'process_id':42,'game':'apex','profile_id':self.a['id'],'config_path':str(self.repo.runtime_path(self.a))}
        self.app.pending_reload=(42,23);self.app.notice.set('等待本次请求')
        status={'phase':'running','record':record}
        with patch.object(self.app.manager,'active',return_value=record):
            self.app.apply_observation({'status':status,'learning':{'completed_id':22,'status':2,'revision':4},'fusion':None})
            self.assertEqual(self.app.notice.get(),'等待本次请求')
            self.assertEqual(self.app.pending_reload,(42,23))
            self.app.apply_observation({'status':status,'learning':{'completed_id':23,'status':2,'revision':5,'message':'learning preserved'},'fusion':None})
            self.assertIsNone(self.app.pending_reload);self.assertIn('已热重载',self.app.notice.get())

    def test_slow_runtime_observation_keeps_ui_event_loop_responsive(self):
        entered=threading.Event();release=threading.Event();query_threads=[]
        def status():
            query_threads.append(threading.get_ident());entered.set()
            release.wait(3)
            return {'phase':'stopped','record':None}
        self.root.after_cancel(self.app.poll_id)
        try:
            with patch.object(self.app.manager,'status',side_effect=status):
                self.app.next_observation=0.;self.app.poll()
                self.assertTrue(entered.wait(1))
                heartbeat=[]
                self.root.after(0,lambda:heartbeat.append(True))
                self.app.variables['gamepad.ads.strength_scale'].set('.74')
                self.root.update()
                self.assertEqual(heartbeat,[True])
                self.assertEqual(self.app.state['raw']['gamepad.ads.strength_scale'],'.74')
                self.assertTrue(all(t!=threading.get_ident() for t in query_threads))
        finally:release.set()

    def test_observation_started_before_mutation_cannot_clear_pending_reload(self):
        self.app.pending_reload=(42,23)
        self.app.observation_after=time.monotonic()
        self.app.observer.results.put({'sampled_at':self.app.observation_after-1,
            'status':{'phase':'stopped','record':None},'learning':None,'fusion':None})
        self.root.after_cancel(self.app.poll_id)
        with patch.object(self.app.observer,'request'):
            self.app.poll()
        self.assertEqual(self.app.pending_reload,(42,23))

    def test_curve_canvas_and_precision_toolbar_are_visible_in_minimum_window(self):
        self.root.geometry('840x680+20+20');self.app.show_page('curve');self.root.update()
        bottom=self.app.surface.canvas.winfo_rooty()+self.app.surface.canvas.winfo_height()
        for widget in [self.app.curve_editor,self.app.precision_button,self.app.add_point_button,self.app.remove_point_button]:
            self.assertLessEqual(widget.winfo_rooty()+widget.winfo_height(),bottom)

    def test_rebuilding_surfaces_releases_interpreter_class_binding_commands(self):
        for page in ('curve','device','feedback','assist'):self.app.show_page(page)
        self.assertFalse([name for name in self.host._tclCommands or [] if not self.host.tk.call('info','commands',name)])

    def test_retained_numeric_widgets_release_tk_variables_on_window_destroy_before_worker_collection(self):
        widget,_=self.app.field_widgets['gamepad.ads.strength_scale']
        self.app.show_page('device')
        self.assertTrue(widget.winfo_exists())
        self.app.closed=True;self.root.after_cancel(self.app.poll_id);self.root.destroy()
        self.assertIsNone(widget.variable)
        self.assertIsNone(widget.knob)
        self.assertIsNone(widget.scale.variable)
        with patch('sys.unraisablehook') as unraisable:
            worker=threading.Thread(target=gc.collect);worker.start();worker.join()
            unraisable.assert_not_called()

    def test_close_preserves_runtime_and_fusion(self):
        with patch.object(self.app.manager,'stop') as stop,patch.object(self.app.manager,'set_fusion') as fusion:
            self.app.close();stop.assert_not_called();fusion.assert_not_called()
