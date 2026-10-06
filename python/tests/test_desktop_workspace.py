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
from desktop_app.test_desktop import require_isolated_gui

PROJECT=Path(__file__).resolve().parents[2]
PRODUCTION=PROJECT/'native/build/Release/cod_native_runtime.exe'


class IndependentStorageTests(unittest.TestCase):
    def test_every_editable_numeric_field_roundtrips_native_without_replacement(self):
        from desktop_app.workspace import FIELDS,put
        from desktop_app.fields import field_value
        manager=RuntimeManager(PROJECT)
        for field in FIELDS:
            path,label,kind,default,limits=field
            if kind not in (int,float):continue
            lo,hi=limits
            for value in (lo,default,hi):
                with self.subTest(path=path,value=value):
                    document={}
                    put(document,'gamepad.ads.activation_trigger',0.)
                    put(document,'gamepad.ads.scope_ready_trigger',1.)
                    put(document,'gamepad.ads.completion_radius_px',1.)
                    put(document,'gamepad.assist.arrival_radius_px',1.)
                    put(document,'gamepad.bodylock.activation_range_px',2000.)
                    put(document,'gamepad.ai_aim.manual_intent_begin',0.)
                    put(document,'gamepad.ai_aim.manual_intent_full',1.)
                    put(document,'gamepad.auto_fire.pulse_width_ms',1.)
                    put(document,'gamepad.auto_fire.pulse_period_ms',5000.)
                    put(document,path,field_value(field,value))
                    actual=manager.inspect_defaults('default',toml_text(document))
                    self.assertIn(path,actual)
                    self.assertAlmostEqual(actual[path],value,delta=max(1e-6,abs(value)*1e-6))
            for value in (lo-1,hi+1):
                with self.subTest(path=path,invalid=value), self.assertRaises(ValueError):field_value(field,value)


    def test_legacy_parameters_convert_once_to_explicit_limits_and_times(self):
        from desktop_app.parameter_catalog import configured_value
        source={'gamepad':{'ads':{'strength_scale':.2,'vertical_strength_scale':.4},
            'bodylock':{'strength':2.,'vertical_strength':.3,'feedback_distance_px':27},
            'ai_aim':{'hipfire_multiplier':2.},'recoil':{'feedback_amount':0.}}}
        before=deepcopy(source)
        converted=snapshot(source,'apex',PROJECT)['config']
        self.assertAlmostEqual(lookup(converted,'gamepad.ads.output_limit_x'),.2*2**.5,places=6)
        self.assertEqual(lookup(converted,'gamepad.bodylock.output_limit_x'),1.)
        self.assertEqual(lookup(converted,'gamepad.bodylock.response_time_x_ms'),27.)
        self.assertEqual(lookup(converted,'gamepad.assist.hipfire_ratio'),1.)
        self.assertEqual(lookup(converted,'gamepad.recoil.output_amount'),0.)
        self.assertEqual(source,before)
        converted['gamepad']['bodylock']['output_limit_x']=.2
        self.assertEqual(configured_value(converted,'gamepad.bodylock.response_time_x_ms'),27.)
        values=RuntimeManager(PROJECT).inspect_defaults('apex',toml_text(converted))
        for path in ('gamepad.ads.output_limit_x','gamepad.bodylock.output_limit_x',
                     'gamepad.bodylock.response_time_x_ms','gamepad.assist.hipfire_ratio','gamepad.recoil.output_amount'):
            self.assertAlmostEqual(values[path],lookup(converted,path),places=5)
    def test_legacy_feedback_import_preserves_effective_distance_and_source(self):
        for path in ('gamepad.bodylock.tolerance_px','gamepad.ai_aim.body_lock_box_tolerance_px'):
            from desktop_app.workspace import put,configured_value
            for value in (0,1,8,12,18,24,2000):
                document={};put(document,path,value);before=deepcopy(document)
                migrated=snapshot(document,'apex',PROJECT)['config']
                self.assertAlmostEqual(lookup(migrated,'gamepad.bodylock.response_time_x_ms'),min(1000,max(18,value*1.5)/.15),places=3)
                self.assertEqual(document,before)
                self.assertEqual(configured_value(document,'gamepad.bodylock.feedback_distance_px'),max(18,value*1.5))
            put(document,'gamepad.bodylock.feedback_distance_px',1)
            self.assertEqual(configured_value(document,'gamepad.bodylock.feedback_distance_px'),1)
        with self.assertRaises(ValueError):snapshot({'gamepad':{'bodylock':{'feedback_distance_px':0}}},'apex',PROJECT)

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
        saved['name']='Apex 训练';saved['config']['gamepad']['ads']['output_limit_x']=.735
        self.repo.save(saved,expected)
        reopened=self.repo.read(self.repo.path(a['id']))[0]
        self.assertEqual(reopened['curve']['algorithm'],'linear')
        self.assertEqual(reopened['id'],a['id'])
        self.assertEqual(lookup(self.repo.read(self.repo.path(b['id']))[0]['config'],'gamepad.ads.output_limit_x'),1.)
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
        text='[runtime]\ngame="default"\n[gamepad.ads]\noutput_limit_x=.8\n[games.apex.gamepad.ads]\noutput_limit_x=.6\n'
        # TOML requires leading zero on floats.
        text=text.replace('=.','=0.')
        path=self.root/'import.toml';path.write_text(text,encoding='utf-8')
        p=self.repo.import_file(path,'导入','apex')
        self.assertEqual(lookup(p['config'],'gamepad.ads.output_limit_x'),.6)
        self.assertNotIn('games',p['config'])
        self.assertEqual(path.read_text(encoding='utf-8'),text)
        source=tomllib.loads(text);before=deepcopy(source)
        snapshot(source,'bo3',PROJECT)
        self.assertEqual(source,before)
        common={'runtime':{'game':'default'},'gamepad':{'ads':{'output_limit_x':.9}}}
        self.assertEqual(lookup(snapshot(common,'apex',PROJECT)['config'],'gamepad.ads.output_limit_x'),.9)

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
    def test_output_percentages_share_units_limits_and_saved_meaning(self):
        self.app.show_page('ads');self.root.update()
        paths=('gamepad.ads.output_limit_x','gamepad.ads.output_limit_y',
               'gamepad.bodylock.output_limit_x','gamepad.bodylock.output_limit_y')
        for path in paths:
            widget,_=self.app.field_widgets[path]
            self.assertEqual(widget.rail.unit.cget('text'),'%')
            widget.display.set('35')
            self.assertEqual(float(self.app.variables[path].get()),.35)
            widget.display.set('101')
            self.assertTrue(widget.entry.instate(['invalid']))
            with self.assertRaises(ValueError):self.app.collect()
            widget.display.set('35')
        saved=self.app.collect()
        for path in paths:self.assertEqual(lookup(saved['config'],path),.35)
        self.assertFalse(any(p.endswith(('strength_scale','vertical_strength_scale','feedback_distance_px')) for p in self.app.field_widgets))

    def test_preview_changes_timing_independently_of_unsaturated_limit(self):
        self.app.show_page('ads');self.root.update()
        deadline=time.monotonic()+10
        while self.app.ads_policy_loading and time.monotonic()<deadline:
            self.app.poll();self.root.update();time.sleep(.01)
        diagram=self.app.ads_diagram;diagram.mode.set('follow')
        self.app.variables['gamepad.assist.minimum_position_stick'].set('0')
        diagram.offset=[3.,0.];diagram.redraw()
        original=diagram.geometry['example']['stick'][0]
        self.app.variables['gamepad.bodylock.output_limit_x'].set('.9')
        self.assertEqual(diagram.geometry['example']['stick'][0],original)
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('90')
        import math
        self.assertAlmostEqual(diagram.geometry['example']['stick'][0],original*2,places=6)
    @classmethod
    def setUpClass(cls):
        require_isolated_gui()
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

    def test_curve_selection_commits_precision_draft_before_changing_point(self):
        self.app.show_page('curve');self.root.update()
        editor=self.app.curve_editor
        editor.select(3)
        self.app.point_y.set('25')
        editor.select(4)
        self.assertEqual(editor.points[3][1],.25)
        self.app.point_y.set('bad')
        editor.select(5)
        self.assertEqual(editor.selected,4)
        self.assertEqual(self.app.point_y.get(),'bad')

    def test_compensation_disclosure_belongs_to_parameters_and_preserves_values(self):
        from desktop_app.components import DisclosureButton
        before=deepcopy(self.app.state['raw'])
        def descendants(widget):
            for child in widget.winfo_children():
                yield child
                yield from descendants(child)
        button=next(w for w in descendants(self.app.surface) if isinstance(w,DisclosureButton) and w.label=='手动输出补偿')
        button.invoke();self.root.update()
        self.assertTrue(self.app.transfer_group.winfo_ismapped())
        self.assertEqual(self.app.state['raw'],before)
        self.assertFalse(self.app.dirty())
        button.invoke();self.root.update()
        self.assertFalse(self.app.transfer_group.winfo_ismapped())
        self.app.search.set('output_transfer');self.root.update()
        self.assertTrue(self.app.transfer_group.winfo_ismapped())
        self.app.search.set('');self.app.show_page('device');self.root.update()
        self.assertFalse(any(p.startswith('gamepad.output_transfer.') for p in self.app.field_widgets))

    def test_curve_hover_is_a_read_only_piecewise_response_preview(self):
        from types import SimpleNamespace
        self.app.show_page('curve');self.root.update()
        editor=self.app.curve_editor
        editor.move_point(3,.3,.25)
        before=deepcopy(editor.points);history=len(editor.model.undo_stack)
        x,y=editor.pixel(.35,.35)
        editor.probe(SimpleNamespace(x=x,y=y))
        readout=editor.itemcget(editor.paint_items['readout'][0],'text')
        self.assertIn('35.0%',readout)
        self.assertIn('32.5%',readout)
        self.assertEqual(editor.points,before)
        self.assertEqual(len(editor.model.undo_stack),history)

    def test_curve_type_change_preserves_precision_as_an_undoable_edit(self):
        self.app.show_page('curve');self.root.update()
        editor=self.app.curve_editor;editor.select(3)
        self.app.point_y.set('25')
        self.app.curve_choice.set('linear')
        self.assertEqual(editor.points[3][1],.3)
        editor.undo()
        self.assertEqual(editor.points[3][1],.25)
        self.assertEqual(self.app.profile['curve']['algorithm'],'custom_lut')

    def test_dense_parameter_page_has_twelve_visible_fields_at_900_by_740(self):
        self.assertEqual(len(self.app.field_widgets),13)
        bottom=self.app.surface.canvas.winfo_rooty()+self.app.surface.canvas.winfo_height()
        for widget,_ in self.app.field_widgets.values():
            self.assertLessEqual(widget.winfo_rooty()+widget.winfo_height(),bottom)
        self.assertEqual(len(self.app.navigation),5)
        self.assertFalse(hasattr(self.app,'game'))

    def test_form_numeric_columns_slider_tracks_and_row_spacing_are_consistent(self):
        paths=('gamepad.ads.output_limit_x','gamepad.ads.output_limit_y')
        widgets=[self.app.field_widgets[path][0] for path in paths]
        self.root.update()
        self.assertTrue(all(w.display.get()=='100' for w in widgets))
        self.assertEqual(len({w.entry.winfo_rootx() for w in widgets}),1)
        for w in widgets:self.assertEqual(w.entry.winfo_width(),widgets[0].entry.winfo_width())
        self.app.show_page('ads');self.root.update()
        for paths in (('gamepad.ads.pickup_base_radius_px','gamepad.ads.output_limit_x','gamepad.ads.output_limit_y'),
                      ('gamepad.bodylock.activation_range_px','gamepad.bodylock.output_limit_x','gamepad.bodylock.output_limit_y','gamepad.bodylock.response_time_x_ms')):
            self.app.ads_diagram.mode.set('follow' if paths[0].startswith('gamepad.bodylock') else 'acquire')
            self.root.update()
            editors=[getattr(self.app.field_widgets[p][0],'entry',self.app.field_widgets[p][0]) for p in paths]
            self.assertEqual(len({e.winfo_rootx() for e in editors}),1)
            self.assertEqual(len({e.winfo_width() for e in editors}),1)
            for previous,current in zip(editors,editors[1:]):
                self.assertGreaterEqual(current.winfo_rooty()-previous.winfo_rooty()-previous.winfo_height(),4)

    def test_preview_stage_selection_is_explicit_and_survives_parameter_edits(self):
        self.app.show_page('ads');self.root.update()
        deadline=time.monotonic()+10
        while self.app.ads_policy_loading and time.monotonic()<deadline:
            self.app.poll();self.root.update();time.sleep(.01)
        diagram=self.app.ads_diagram
        self.assertEqual(diagram.mode.get(),'acquire')
        self.assertEqual(diagram.active_mode,'acquire')
        self.assertIn('ADS',diagram.mode_button.buttons[diagram.active_mode].cget('text'))
        self.assertIn('首次瞄准',diagram.feedback_summary.get())
        items=diagram.canvas.find_all()
        self.assertFalse(self.app.dirty())
        diagram.mode.set('follow');self.root.update()
        self.assertTrue(self.app.ads_groups['follow'].winfo_ismapped())
        self.assertFalse(self.app.ads_groups['acquire'].winfo_ismapped())
        diagram.place_example(1.15)
        self.assertIn('圈外',diagram.summary.get())
        diagram.place_example(.8)
        self.assertIn('圈内',diagram.summary.get())
        diagram.offset=[25.,0.];diagram.redraw()
        self.assertFalse(self.app.dirty(),'preview mode is not a game setting')
        self.assertIn('跟随',diagram.mode_button.buttons[diagram.active_mode].cget('text'))
        self.app.variables['gamepad.ads.output_limit_x'].set('.05')
        self.assertEqual(diagram.active_mode,'follow','manual mode must survive parameter edits')
        diagram.mode_button.buttons['acquire'].invoke();self.root.update()
        self.assertEqual(diagram.active_mode,'acquire')
        small=diagram.geometry['example']['stick'][0]
        self.app.variables['gamepad.ads.output_limit_x'].set('1')
        self.assertGreater(diagram.geometry['example']['stick'][0],small)
        self.app.field_widgets['gamepad.bodylock.output_limit_x'][0].entry.event_generate('<FocusIn>')
        self.assertEqual(diagram.active_mode,'acquire','focus does not switch stages')
        diagram.mode_button.buttons['follow'].invoke()
        self.assertEqual(diagram.active_mode,'follow')
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('bad')
        self.assertEqual(diagram.example_output.get(),'')
        diagram.mode.set('acquire')
        self.assertIsNotNone(diagram.geometry,'an invalid inactive follow draft must not hide ADS')
        self.app.variables['gamepad.bodylock.activation_range_px'].set('bad')
        self.assertIsNotNone(diagram.geometry)
        self.assertIsNone(diagram.geometry['follow_radius'])
        self.app.variables['gamepad.bodylock.activation_range_px'].set('150')
        self.app.variables['gamepad.ads.output_limit_y'].set('bad')
        self.assertEqual(diagram.example_output.get(),'')
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('27')
        diagram.mode.set('follow')
        self.assertIsNotNone(diagram.geometry,'an invalid inactive ADS draft must not hide follow')
        self.app.variables['gamepad.ads.pickup_base_radius_px'].set('bad')
        self.assertIsNotNone(diagram.geometry)
        self.assertIsNone(diagram.geometry['radius'])
        self.app.variables['gamepad.ads.pickup_base_radius_px'].set('150')
        self.assertEqual(diagram.canvas.find_all(),items)
        self.app.variables['gamepad.ads.output_limit_y'].set('1')
        diagram.mode.set('acquire');diagram.offset=[220,0];diagram.redraw()
        self.assertIn('不能新拾取',diagram.feedback_summary.get())
        self.assertIn('不输出辅助',diagram.example_output.get())
        self.app.variables['gamepad.bodylock.activation_range_px'].set('300')
        diagram.mode.set('follow');diagram.redraw()
        self.assertIn('向右',diagram.example_output.get(),'selected-target follow can continue outside the ADS pickup circle')

    def test_range_preview_resize_stays_within_viewport_without_layout_feedback(self):
        self.app.show_page('ads');self.root.update()
        deadline=time.monotonic()+10
        while self.app.ads_policy_loading and time.monotonic()<deadline:
            self.app.poll();self.root.update();time.sleep(.01)
        diagram=self.app.ads_diagram
        changes=[]
        binding=self.app.surface.canvas.bind('<Configure>',lambda e:changes.append((e.width,e.height)),add='+')
        try:
            for mode,width,height in ((mode,w,h) for mode in ('acquire','follow')
                    for w,h in ((900,740),(840,680),(1120,840),(852,680),(840,680))):
                diagram.mode.set(mode)
                start=len(changes)
                self.root.geometry(f'{width}x{height}+20+20');self.root.update()
                self.assertLess(len(changes)-start,8,'a width change must settle instead of toggling scrollbar visibility')
                self.assertEqual(self.app.surface.canvas.yview(),(0.,1.))
                for label in (diagram.summary_label,diagram.feedback_label):
                    self.assertEqual(label.winfo_height(),label.winfo_reqheight(),'captions must not be clipped by the canvas')
                self.assertGreaterEqual(diagram.canvas.winfo_height(),200)
                self.assertLessEqual(diagram.mode_button.winfo_rootx()+diagram.mode_button.winfo_width(),
                                     diagram.winfo_rootx()+diagram.winfo_width())
        finally:self.app.surface.canvas.unbind('<Configure>',binding)

    def test_input_device_menu_uses_names_and_persists_selection_without_slot_numbers(self):
        # This test owns the enumeration fixture; the background scan has its
        # own test and must not replace these names with connected hardware.
        self.app.devices_scanned=True
        self.app.show_page('device');self.root.update()
        self.assertIn('runtime.input.device_id',self.app.field_widgets,'device selection must determine native input')
        self.assertNotIn('runtime.input.controller_index',self.app.field_widgets)
        self.assertNotIn('runtime.input.auto_detect',self.app.field_widgets)
        devices=[{'id':'path:aaaa','name':'DualSense Wireless Controller'},
                 {'id':'path:bbbb','name':'Xbox Wireless Controller'}]
        self.app.input_devices=devices
        self.app.refresh_device_choices()
        choice,_=self.app.field_widgets['runtime.input.device_id']
        self.assertIsInstance(choice,ChoiceInput)
        self.assertEqual(choice.labels['path:aaaa'],'DualSense Wireless Controller')
        choice.choose('path:aaaa')
        candidate=self.app.collect()
        self.assertEqual(lookup(candidate['config'],'runtime.input.device_id'),'path:aaaa')
        self.assertEqual(lookup(candidate['config'],'runtime.input.device_name'),'DualSense Wireless Controller')
        with patch.object(self.app.manager,'active',return_value=None):
            self.app.save();self.finish()
        saved,_=self.repo.read(self.repo.path(self.a['id']))
        self.assertEqual(lookup(saved['config'],'runtime.input.device_id'),'path:aaaa')
        values=self.app.manager.inspect_defaults('apex',toml_text(projection(saved)))
        self.assertEqual(values['runtime.input.device_id'],'path:aaaa')
        self.app.input_devices=[];self.app.refresh_device_choices()
        self.assertIn('DualSense',choice.labels['path:aaaa'])
        self.assertIn('未连接',choice.labels['path:aaaa'])
        self.assertEqual(self.app.variables['runtime.input.device_id'].get(),'path:aaaa')
        self.app.select_profile(self.b['id'])
        self.assertEqual(choice.variable.get(),'')
        self.app.select_profile(self.a['id'])
        self.assertEqual(choice.variable.get(),'path:aaaa')
        choice.choose('')
        self.assertEqual(lookup(self.app.collect()['config'],'runtime.input.device_id'),'')
        paths=['hid#vid_sony#usb_port_left#{shared-guid}','hid#vid_sony#usb_port_right#{shared-guid}']
        self.app.input_devices=[{'id':'path:'+p.encode().hex(),'name':'DualSense Wireless Controller'} for p in paths]
        self.app.refresh_device_choices()
        labels=[choice.labels[d['id']] for d in self.app.input_devices]
        self.assertNotEqual(*labels)
        choice.choose(self.app.input_devices[0]['id'])
        for geometry in ('900x740','840x680'):
            self.root.geometry(geometry);self.root.update()
            self.assertLessEqual(choice.winfo_rootx()+choice.winfo_width(),self.root.winfo_rootx()+self.root.winfo_width())
        choice.open();self.root.update()
        self.assertIn('DualSense Wireless Controller',choice.menu_rows[1].cget('text'))
        choice.dismiss()

    def test_device_scan_runs_off_tk_thread_and_does_not_block_editing(self):
        started=threading.Event();release=threading.Event()
        def scan():
            self.assertNotEqual(threading.current_thread(),threading.main_thread())
            started.set();release.wait(3)
            return [{'id':'name:abcd','name':'测试手柄'}]
        with patch.object(self.app.manager,'input_devices',side_effect=scan):
            try:
                self.app.show_page('device')
                self.assertTrue(started.wait(1))
                self.assertFalse(self.app.busy)
                calls=[];self.root.after(5,lambda:calls.append(True))
                deadline=time.monotonic()+1
                while not calls and time.monotonic()<deadline:self.root.update()
                self.assertEqual(calls,[True])
                self.app.show_page('assist')
                self.app.variables['gamepad.ads.output_limit_x'].set('.75')
                self.assertEqual(self.app.state['raw']['gamepad.ads.output_limit_x'],'.75')
            finally:release.set()
            deadline=time.monotonic()+2
            while self.app.device_scanning and time.monotonic()<deadline:self.app.poll();self.root.update()
            self.assertFalse(self.app.device_scanning)
        self.app.show_page('device')
        choice,_=self.app.field_widgets['runtime.input.device_id']
        self.assertEqual(choice.labels['name:abcd'],'测试手柄')

    def test_memory_frame_rates_are_weighted_by_aim_time_and_follow_running_profile(self):
        self.app.show_page('feedback');self.root.update()
        self.assertTrue(hasattr(self.app,'frame_rate_values'),'FPS must be visible without opening logs')
        record={'process_id':42,'process_created':77,'game':'apex','profile_id':self.b['id']}
        performance={'pid':42,'created':77,'state':1,'sampled_at_ms':1,
            'elapsed_ns':10_000_000_000,'aim_ns':5_000_000_000,'vision_frames':1120,'aim_frames':820,
            'controller_ticks':5000,'recent_elapsed_ns':5_000_000_000,'recent_aim_ns':4_000_000_000,
            'recent_vision_frames':640,'recent_aim_frames':640,'aiming':True}
        self.app.apply_observation({'status':{'phase':'running','record':record},'learning':None,'fusion':False,'performance':performance})
        self.assertEqual([v.get() for v in self.app.frame_rate_values],['112.0','164.0','160.0'])
        self.assertIn(self.b['name'],self.app.frame_rate_detail.get())
        self.assertIn('820',self.app.frame_rate_detail.get())
        self.app.select_profile(self.a['id']);self.root.update()
        self.assertEqual(self.app.frame_rate_values[1].get(),'164.0')
        self.app.apply_observation({'status':{'phase':'stopped','record':None},'learning':None,'fusion':False,'performance':None})
        self.assertEqual(self.app.frame_rate_values[1].get(),'164.0')
        self.assertIn('已停止',self.app.frame_rate_detail.get())

        # Cached pages restore the last observed RAM snapshot, and a new
        # process session clears it even before the first counters arrive.
        self.app.show_page('assist');self.app.show_page('feedback');self.root.update()
        self.assertEqual(self.app.frame_rate_values[1].get(),'164.0')
        next_record=dict(record,process_created=78)
        self.app.apply_observation({'status':{'phase':'running','record':next_record},'learning':None,'fusion':False,'performance':None})
        self.assertEqual([v.get() for v in self.app.frame_rate_values],['—','—','—'])
        next_value=dict(performance,aim_ns=0,aim_frames=0,recent_aim_ns=0,recent_aim_frames=0,aiming=False)
        self.app.apply_observation({'status':{'phase':'running','record':next_record},'learning':None,'fusion':False,'performance':next_value})
        self.assertEqual([v.get() for v in self.app.frame_rate_values],['112.0','—','—'])
        stalled=dict(next_value,aim_ns=5_000_000_000,recent_aim_ns=4_000_000_000,aiming=True)
        self.app.apply_observation({'status':{'phase':'running','record':next_record},'learning':None,'fusion':False,'performance':stalled})
        self.assertEqual([v.get() for v in self.app.frame_rate_values],['112.0','0.0','0.0'])

    def test_ads_fire_delay_saves_and_requests_hot_reload_for_the_running_profile(self):
        path='gamepad.auto_fire.ads_press_delay_ms'
        self.assertIn(path,self.app.field_widgets,'L2 fire suppression window must be editable')
        widget,_=self.app.field_widgets[path]
        self.assertIsInstance(getattr(widget,'entry',widget),ttk.Entry,'millisecond duration needs precise numeric input')
        for invalid in ('-1','5000.1','nan','inf'):
            self.app.variables[path].set(invalid)
            with self.assertRaises(ValueError):self.app.collect()
        self.app.variables[path].set('5000')
        self.assertEqual(lookup(self.app.collect()['config'],path),5000)
        self.app.variables[path].set('125.5')
        record={'process_id':42,'game':'apex','config_path':str(self.repo.runtime_path(self.a)),
                'stdout_path':str(self.temp.name+'/stdout.log'),'stderr_path':str(self.temp.name+'/stderr.log')}
        with patch.object(self.app.manager,'active',return_value=record), \
             patch.object(self.app.manager,'reload_config',return_value={'status':2,'revision':7,'message':'applied'}) as reload:
            self.app.save();self.finish()
            reload.assert_called_once()
            self.assertIn('已热重载',self.app.notice.get())
            self.app.show_page('device');self.app.apply_saved_config();self.finish()
            self.assertEqual(reload.call_count,2)
        saved,_=self.repo.read(self.repo.path(self.a['id']))
        self.assertEqual(lookup(saved['config'],path),125.5)
        self.assertEqual(self.app.manager.inspect_defaults('apex',toml_text(projection(saved)))[path],125.5)
        self.app.select_profile(self.b['id'])
        self.assertEqual(self.app.variables[path].get(),'0.0')
        with patch.object(self.app.manager,'active',return_value=record),patch.object(self.app.manager,'reload_config') as reload:
            self.app.variables[path].set('60');self.app.save();self.finish()
            reload.assert_not_called()

    def test_catalog_advanced_parameters_search_persist_relations_and_native_defaults(self):
        from desktop_app.parameter_catalog import PARAMETERS
        self.app.search.set('忽略手动意图');self.root.update()
        self.assertTrue(self.app.advanced.get())
        self.assertIn('gamepad.ai_aim.manual_intent_begin',self.app.field_widgets)
        self.app.search.set('')
        for path,parameter in PARAMETERS.items():
            if parameter.get('legacy'):continue
            self.app.show_page(parameter.get('page','assist'))
            self.assertIn(path,self.app.field_widgets)
            self.assertAlmostEqual(float(self.app.variables[path].get()),parameter['default'])
        begin='gamepad.ai_aim.manual_intent_begin';full='gamepad.ai_aim.manual_intent_full'
        self.app.variables[begin].set('.4');self.app.variables[full].set('.4')
        with self.assertRaisesRegex(ValueError,'必须小于'):self.app.collect()
        self.app.variables[begin].set('.1');self.app.variables[full].set('.25')
        self.app.variables['gamepad.auto_fire.pulse_width_ms'].set('200')
        with self.assertRaisesRegex(ValueError,'不能大于'):self.app.collect()
        self.app.variables['gamepad.auto_fire.pulse_width_ms'].set('20')
        self.app.variables['gamepad.auto_fire.pulse_period_ms'].set('80')
        self.app.save();self.finish()
        self.assertFalse(self.app.dirty(),self.app.notice.get())
        saved,_=self.repo.read(self.repo.path(self.a['id']))
        values=self.app.manager.inspect_defaults('apex',toml_text(projection(saved)))
        self.assertAlmostEqual(values[begin],.1);self.assertAlmostEqual(values[full],.25)
        self.assertEqual(values['gamepad.auto_fire.pulse_width_ms'],20)
        self.app.select_profile(self.b['id'])
        self.assertEqual(self.app.variables[begin].get(),'0.15')

    def test_ads_geometry_page_drag_pose_size_and_profile_isolation(self):
        self.app.show_page('ads');self.root.update()
        deadline=time.monotonic()+10
        while self.app.ads_policy_loading and time.monotonic()<deadline:
            self.app.poll();self.root.update();time.sleep(.01)
        self.assertIsNotNone(self.app.ads_policy,self.app.notice.get())
        diagram=self.app.ads_diagram
        self.root.geometry('840x680+20+20');self.root.update()
        for pose in ('standing','crouching','wide'):
            diagram.pose.set(pose);self.root.update()
            bottom=self.app.surface.canvas.winfo_rooty()+self.app.surface.canvas.winfo_height()
            self.assertEqual(self.app.surface.canvas.yview(),(0.,1.),
                             'the default range page must fit entirely in the first viewport')
            for widget in [diagram.canvas,diagram.feedback_label]+[widget for widget,_ in self.app.field_widgets.values()]:
                self.assertLessEqual(widget.winfo_rooty()+widget.winfo_height(),bottom,
                                     f'{pose}: primary ADS and BodyLock controls must fit the minimum window')
        diagram.pose.set('standing');self.root.update()
        self.assertAlmostEqual(diagram.geometry['radius'],178.125,places=3)
        items=diagram.canvas.find_all()
        for percent in (10,25,50):
            diagram.height.set(str(percent));self.root.update()
            self.assertAlmostEqual(diagram.geometry['radius'],150*(1+.75*percent/100),places=3)
        diagram.height.set('1');self.assertFalse(diagram.geometry['pickup'])
        self.assertIn('不满足',diagram.summary.get())
        diagram.height.set('50');self.assertTrue(diagram.geometry['pickup'])
        diagram.pose.set('wide');self.root.update()
        self.assertTrue(diagram.geometry['wide_low'])
        self.assertIn('宽矮',diagram.summary.get())
        x0,y0,scale,width,height=diagram.transform
        diagram.canvas.event_generate('<Button-1>',x=int(x0),y=int(y0))
        self.assertIn('圈外',diagram.summary.get())
        diagram.canvas.event_generate('<B1-Motion>',x=int(x0+width*scale/2),y=int(y0+height*scale/2))
        self.assertIn('圈内',diagram.summary.get())
        diagram.compare.set(True)
        self.assertIn('蓝圈 ADS',diagram.legend.cget('text'))
        self.assertIn('紫圈跟随',diagram.legend.cget('text'))
        self.assertFalse(self.app.dirty(),'example size and drag must not change the saved profile')
        self.app.variables['gamepad.ads.activation_trigger'].set('.1')
        self.app.variables['gamepad.ads.pickup_base_radius_px'].set('175.5')
        self.assertAlmostEqual(diagram.geometry['radius'],175.5*1.375,places=3)
        self.assertEqual(diagram.canvas.find_all(),items,'drawing must reuse canvas items')
        self.app.variables['gamepad.bodylock.activation_range_px'].set('220.5')
        diagram.mode.set('follow')
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('36.375')
        self.assertAlmostEqual(diagram.geometry['follow_radius'],220.5*1.375,places=3)
        self.assertAlmostEqual(diagram.geometry['response_time_ms'][0],36.375)
        self.assertAlmostEqual(diagram.geometry['cue_follow_radius'],220.5)
        self.assertIn('跟随',diagram.summary.get())
        diagram.offset=[5,-7];diagram.redraw()
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('5')
        baseline=diagram.geometry['example']['stick']
        self.assertEqual(diagram.geometry['response_time_ms'][0],5)
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('36.375')
        softer=diagram.geometry['example']['stick']
        self.assertLess(softer[0],baseline[0],'a softer setting must reduce example correction for the same position')
        vertical=softer[1]
        self.app.variables['gamepad.bodylock.output_limit_y'].set('.2')
        self.assertLessEqual(diagram.geometry['example']['stick'][1],.2)
        rings=[diagram.canvas.coords(diagram.items[key]) for key in ('radius','follow_radius')]
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('45')
        self.assertEqual([diagram.canvas.coords(diagram.items[key]) for key in ('radius','follow_radius')],rings)
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('36.375')
        self.app.variables['gamepad.assist.arrival_radius_px'].set('32')
        self.app.variables['gamepad.bodylock.activation_range_px'].set('4')
        self.assertIsNone(diagram.geometry)
        self.assertIn('不能小于',diagram.summary.get())
        self.app.variables['gamepad.assist.arrival_radius_px'].set('2')
        diagram.set_config(self.app.profile['config'])
        self.app.variables['gamepad.bodylock.activation_range_px'].set('220.5')
        self.app.save();self.finish();self.assertFalse(self.app.dirty(),self.app.notice.get())
        saved=self.repo.read(self.repo.path(self.a['id']))[0]['config']
        self.assertEqual(lookup(saved,'gamepad.bodylock.activation_range_px'),220.5)
        self.assertEqual(lookup(saved,'gamepad.bodylock.response_time_x_ms'),36.375)
        self.app.select_profile(self.b['id']);self.root.update()
        self.assertEqual(self.app.variables['gamepad.ads.activation_trigger'].get(),'0.05')
        self.assertEqual(self.app.variables['gamepad.ads.pickup_base_radius_px'].get(),'150.0')
        self.assertEqual(self.app.variables['gamepad.bodylock.activation_range_px'].get(),'150.0')
        self.assertEqual(self.app.variables['gamepad.bodylock.response_time_x_ms'].get(),'180.0')
        self.app.variables['gamepad.ads.scope_ready_trigger'].set('.04')
        with self.assertRaisesRegex(ValueError,'必须小于'):self.app.collect()
        self.assertIsNotNone(diagram.geometry,'invalid L2 settings must not hide the independent spatial comparison')
        self.app.show_page('assist');self.app.show_page('ads')
        self.assertIs(self.app.ads_diagram,diagram)

    def test_aim_stage_names_and_concrete_follow_example(self):
        from desktop_app.gui import PAGES,SHORT_LABELS
        self.assertIn('输出上限',SHORT_LABELS.get('gamepad.ads.output_limit_x',''))
        self.assertEqual(PAGES['ads'][0],'范围与跟随')
        self.app.show_page('ads');self.root.update()
        deadline=time.monotonic()+10
        while self.app.ads_policy_loading and time.monotonic()<deadline:
            self.app.poll();self.root.update();time.sleep(.01)
        diagram=self.app.ads_diagram
        self.assertTrue(hasattr(diagram,'example_output'))
        diagram.mode.set('follow');self.root.update()
        self.assertNotIn('feedback_current',diagram.items)
        horizontal=self.app.field_widgets['gamepad.bodylock.output_limit_x'][0]
        self.assertEqual(horizontal.display.get(),'30')
        horizontal.display.set('80')
        self.assertEqual(float(self.app.variables['gamepad.bodylock.output_limit_x'].get()),.8)
        self.app.variables['gamepad.bodylock.output_limit_x'].set('.25')
        self.assertEqual(horizontal.display.get(),'25')
        horizontal.display.set('301')
        self.assertIn('100%',self.app.field_widgets['gamepad.bodylock.output_limit_x'][1].cget('text'))
        self.assertTrue(horizontal.entry.instate(['invalid']))
        horizontal.display.set('30')
        diagram.offset=[5,-7];diagram.redraw()
        self.assertIsNotNone(diagram.geometry,diagram.summary.get())
        self.assertIn('向右',diagram.example_output.get());self.assertIn('向上',diagram.example_output.get())
        self.assertIn('瞄点',diagram.feedback_summary.get())
        self.assertTrue(diagram.example_output.get())
        diagram.offset=[320,256];diagram.redraw()
        self.assertIn('停止',diagram.feedback_summary.get())
        self.assertIn('不输出辅助',diagram.example_output.get())
        diagram.offset=[0,0];diagram.redraw()
        self.assertIn('无需位置纠偏',diagram.example_output.get())
        self.assertIn('准星',diagram.canvas.itemcget(diagram.items['center_label'],'text'))
        self.app.variables['gamepad.assist.minimum_position_stick'].set('0')
        self.app.variables['gamepad.assist.arrival_radius_px'].set('1')
        diagram.offset=[1.1,0];diagram.redraw()
        self.assertIn('死区内，已过滤',diagram.example_output.get())
        self.app.variables['gamepad.assist.input_deadzone'].set('0')
        diagram.redraw();self.assertIn('向右',diagram.example_output.get())
        horizontal.display.set('bad')
        self.assertEqual(self.app.variables['gamepad.bodylock.output_limit_x'].get(),'bad')
        self.assertEqual(diagram.example_output.get(),'')
        horizontal.display.set('31.25')
        self.assertEqual(lookup(self.app.collect()['config'],'gamepad.bodylock.output_limit_x'),.3125)


    def test_legacy_profile_opens_as_independent_parameters_without_rewriting_files(self):
        legacy=self.repo.create('旧版跟随','apex')
        _,expected=self.repo.read(self.repo.path(legacy['id']))
        for state in (legacy,legacy['initial']):
            body=state['config']['gamepad']['bodylock']
            for key in ('output_limit_x','output_limit_y','response_time_x_ms','response_time_y_ms'):
                body.pop(key)
            body.update(tolerance_px=8,strength=.3,vertical_strength=.42)
        self.repo.save(legacy,expected)
        original=self.repo.path(legacy['id']).read_bytes()
        self.app.load_library(select=legacy['id']);self.app.show_page('ads');self.root.update()
        self.assertEqual(self.app.variables['gamepad.bodylock.response_time_x_ms'].get(),'120.0')
        self.assertEqual(self.app.variables['gamepad.bodylock.output_limit_x'].get(),'0.3')
        self.assertEqual(self.repo.path(legacy['id']).read_bytes(),original)
        self.assertFalse(self.app.dirty())
        self.app.variables['gamepad.bodylock.response_time_x_ms'].set('5')
        self.app.save();self.finish()
        saved=self.repo.read(self.repo.path(legacy['id']))[0]
        self.assertEqual(lookup(saved['config'],'gamepad.bodylock.response_time_x_ms'),5)
        self.assertEqual(lookup(saved['config'],'gamepad.bodylock.output_limit_x'),.3)
        values=self.app.manager.inspect_defaults('apex',toml_text(projection(saved)))
        self.assertEqual(values['gamepad.bodylock.response_time_x_ms'],5)
        self.assertAlmostEqual(values['gamepad.bodylock.output_limit_x'],.3)

    def test_navigation_and_profile_switch_reuse_parameter_controls(self):
        surface=self.app.surface
        strength=self.app.field_widgets['gamepad.ads.output_limit_x'][0]
        self.app.variables['gamepad.ads.output_limit_x'].set('bad')
        self.app.show_page('device');self.app.show_page('assist')
        self.assertIs(self.app.surface,surface)
        self.assertIs(self.app.field_widgets['gamepad.ads.output_limit_x'][0],strength)
        self.app.select_profile(self.b['id']);self.app.select_profile(self.a['id'])
        self.assertIs(self.app.field_widgets['gamepad.ads.output_limit_x'][0],strength)
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'bad')
        self.assertTrue(strength.entry.instate(['invalid']))

    def test_parameter_and_curve_edits_do_not_probe_process_or_redraw_unchanged_strip(self):
        with patch.object(self.app.manager,'active') as active:
            self.app.variables['gamepad.ads.output_limit_x'].set('.71')
            with patch.object(self.app.profile_strip,'draw') as draw:
                self.app.variables['gamepad.ads.output_limit_x'].set('.72')
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
        self.assertEqual(self.app.variables['gamepad.bodylock.output_limit_x'].get(),'0.3')
        self.assertEqual(self.app.variables['runtime.vision.capture_fps'].get(),'140')
        self.assertEqual(self.app.variables['runtime.vision.model_path'].get(),'')

    def test_missing_model_keeps_running_instance_and_tracks_successful_save(self):
        record={'game':'apex','config_path':str(self.repo.runtime_path(self.b)),'process_id':44}
        with patch.object(self.app.manager,'active',return_value=record),patch.object(self.app.manager,'stop') as stop:
            self.app.variables['gamepad.ads.output_limit_x'].set('.75')
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
        self.app.variables['gamepad.ads.output_limit_x'].set('.65')
        self.app.variables['gamepad.recoil.output_amount'].set('.3')
        widget,_=self.app.field_widgets['gamepad.ads.output_limit_x']
        widget.entry.event_generate('<Button-3>',x=4,y=4);self.root.update()
        self.app.field_popup.choose('initial')
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'1.0')
        self.assertEqual(self.app.variables['gamepad.recoil.output_amount'].get(),'.3')
        self.assertTrue(self.app.dirty())

    def test_search_reveals_matching_advanced_fields_without_losing_raw_values(self):
        self.app.variables['gamepad.ads.output_limit_x'].set('.745')
        self.app.search.set('ads_free');self.root.update()
        self.assertIn('gamepad.ai_aim.ads_free_initial_scale',self.app.field_widgets)
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'.745')

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
        self.app.variables['gamepad.ads.output_limit_x'].set('.7321')
        exported=self.project/'exported.json'
        with patch('desktop_app.gui.filedialog.asksaveasfilename',return_value=str(exported)):
            self.app.export_profile();self.finish()
        self.assertEqual(lookup(json.loads(exported.read_text(encoding='utf-8'))['config'],'gamepad.ads.output_limit_x'),.7321)
        with patch.object(self.app,'name_dialog',return_value='副本'):
            self.app.manage_value.set('copy');self.finish()
        copied=self.app.profile['id']
        self.assertNotEqual(copied,self.a['id'])
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'0.7321')
        with patch.object(self.app,'name_dialog',return_value='比赛配置'):
            self.app.manage_value.set('rename');self.finish()
        self.assertEqual(self.app.profile['id'],copied)
        self.assertEqual(self.app.profile['name'],'比赛配置')
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.output_limit_x'),1.)

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
        editor.delete('1.0','end');editor.insert('1.0',update_text(original,{'gamepad.ads.output_limit_x':.74567}));button.invoke()
        self.assertFalse(window.winfo_exists());self.assertTrue(self.app.dirty())
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'0.74567')
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.output_limit_x'),1.)

    def test_shared_profile_variable_traces_remain_bounded_and_write_only_active_draft(self):
        variable=self.app.variables['gamepad.ads.output_limit_x']
        traces=variable.trace_info()
        for _ in range(8):
            self.app.select_profile(self.a['id']);self.app.select_profile(self.b['id'])
        self.assertIs(variable,self.app.variables['gamepad.ads.output_limit_x'])
        self.assertEqual(variable.trace_info(),traces)
        variable.set('.77')
        self.assertEqual(self.app.states[self.a['id']]['raw']['gamepad.ads.output_limit_x'],'1.0')
        self.assertEqual(self.app.states[self.b['id']]['raw']['gamepad.ads.output_limit_x'],'.77')

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
        self.app.variables['gamepad.ads.output_limit_x'].set('.735')
        self.app.variables['gamepad.recoil.output_amount'].set('bad')
        self.app.show_page('device');self.app.show_page('assist')
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'.735')
        self.app.select_profile(self.b['id']);self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'1.0')
        self.app.select_profile(self.a['id']);self.assertEqual(self.app.variables['gamepad.recoil.output_amount'].get(),'bad')
        with self.assertRaises(ValueError):self.app.collect()
        self.assertIn('未保存',self.app.change_summary.get())

    def test_save_uses_production_loader_and_reopen_restores_profile_and_curve(self):
        self.app.variables['gamepad.ads.output_limit_x'].set('.735')
        self.app.save();self.finish();self.assertFalse(self.app.dirty(),self.app.notice.get())
        self.assertEqual(lookup(self.repo.read(self.repo.path(self.a['id']))[0]['config'],'gamepad.ads.output_limit_x'),.735)
        self.app.closed=True;self.root.after_cancel(self.app.poll_id);self.root.destroy()
        self.root=tk.Toplevel(self.host);self.app=AssistantWindow(self.root,self.project);self.root.update()
        self.assertEqual(self.app.profile['id'],self.a['id'])
        self.assertEqual(self.app.profile['curve']['algorithm'],'linear')
        self.assertEqual(self.app.variables['gamepad.ads.output_limit_x'].get(),'0.735')

    def test_slider_preserves_typed_precision_and_invalid_text(self):
        widget,_=self.app.field_widgets['gamepad.ads.output_limit_x']
        variable=self.app.variables['gamepad.ads.output_limit_x']
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
        strength,_=self.app.field_widgets['gamepad.assist.hipfire_ratio'];old=self.app.variables['gamepad.assist.hipfire_ratio'].get()
        strength.scale.event_generate('<MouseWheel>',delta=-120);strength.entry.event_generate('<MouseWheel>',delta=-120)
        self.root.update();self.assertEqual(self.app.variables['gamepad.assist.hipfire_ratio'].get(),old)

    def test_short_page_wheel_never_scrolls_content_below_the_top(self):
        self.root.geometry('1120x840+20+20');self.root.update()
        surface=self.app.surface
        self.assertLess(surface.content.winfo_reqheight(),surface.canvas.winfo_height())
        for delta in (120,120,-120,120):
            surface.canvas.event_generate('<MouseWheel>',delta=delta);self.root.update()
            self.assertEqual(surface.canvas.canvasy(0),0,
                             'a page shorter than the viewport must not scroll into empty space')

    def test_disclosure_collapse_and_resize_clamp_scroll_extent_and_reuse_controls(self):
        self.root.geometry('900x740+20+20');self.root.update()
        button=self.app.advanced_button
        self.assertTrue(button.cget('text').startswith('▸'))
        self.assertEqual(button.master,self.app.surface.content)
        self.assertEqual(button.winfo_rootx(),self.app.surface.content.winfo_rootx())
        primary=[widget for widget,_ in self.app.field_widgets.values()]
        self.assertGreaterEqual(button.winfo_rooty(),max(widget.winfo_rooty()+widget.winfo_height() for widget in primary))
        button.invoke();self.root.update()
        group=self.app.advanced_group
        self.assertTrue(group.winfo_ismapped())
        self.assertTrue(button.cget('text').startswith('▾'))
        self.app.surface.canvas.yview_moveto(1);self.root.update()
        self.assertGreater(self.app.surface.canvas.canvasy(0),0)
        button.invoke();self.root.update()
        self.assertFalse(group.winfo_ismapped())
        for geometry in ('1120x840','840x680','900x740'):
            self.root.geometry(geometry);self.root.update()
            self.assertGreaterEqual(self.app.surface.canvas.canvasy(0),0)
        button.invoke();self.root.update()
        self.assertIs(self.app.advanced_group,group)
        self.assertIs(self.app.advanced_button,button)

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
                self.app.variables['gamepad.ads.output_limit_x'].set('.74')
                self.root.update()
                self.assertEqual(heartbeat,[True])
                self.assertEqual(self.app.state['raw']['gamepad.ads.output_limit_x'],'.74')
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
        widget,_=self.app.field_widgets['gamepad.ads.output_limit_x']
        slider,_=self.app.field_widgets['gamepad.assist.hipfire_ratio']
        self.app.show_page('device')
        self.assertTrue(widget.winfo_exists())
        self.app.closed=True;self.root.after_cancel(self.app.poll_id);self.root.destroy()
        self.assertIsNone(widget.variable)
        self.assertIsNone(widget.display)
        self.assertIsNone(slider.knob)
        self.assertIsNone(slider.scale.variable)
        with patch('sys.unraisablehook') as unraisable:
            worker=threading.Thread(target=gc.collect);worker.start();worker.join()
            unraisable.assert_not_called()

    def test_close_preserves_runtime_and_fusion(self):
        with patch.object(self.app.manager,'stop') as stop,patch.object(self.app.manager,'set_fusion') as fusion:
            self.app.close();stop.assert_not_called();fusion.assert_not_called()
