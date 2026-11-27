#!/usr/bin/env python3
"""Standalone numerical/CLI regression; Python standard library only."""
import argparse
import csv
import datetime as dt
import io
import json
import math
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--data-dir', default=str(ROOT / 'data'))
    args = parser.parse_args()
    executable = Path(args.exe).resolve()
    data = Path(args.data_dir).resolve()
    def run(options, success=True):
        command = [str(executable), '--data-dir', str(data)] + list(map(str, options))
        result = subprocess.run(command, capture_output=True, text=True)
        assert (result.returncode == 0) == success, (command, result.stdout, result.stderr)
        if not success:
            return result
        rows = list(csv.DictReader(io.StringIO(result.stdout), delimiter='\t'))
        for row in rows:
            assert len(row) == 7 and 0 <= float(row['Hour_UT']) <= 24
            assert math.isfinite(float(row['VTEC_TECU'])) and float(row['VTEC_TECU']) >= 0
            assert re.fullmatch(r'[0-9]+\.[0-9]{2}', row['VTEC_TECU']), row
        return rows
    reference = json.loads((ROOT / 'tests/reference.json').read_text(encoding='utf-8'))
    checked_hourly = checked_fractional = 0
    with tempfile.TemporaryDirectory(prefix='ieim point test ') as folder:
        temp = Path(folder)
        for case in reference['cases']:
            year, doy, ici, modes = [case[k] for k in ['year','doy','ici','modes']]
            file = temp / 'points.txt'
            file.write_text('Year DOY ICI Longitude Latitude\n' + ''.join(
                f'{year} {doy} {ici} {point["longitude"]} {point["latitude"]}\n' for point in case['points']), encoding='utf-8')
            rows = run(['--input', file, '--modes', modes])
            assert len(rows) == len(case['points']) * 25
            for i, point in enumerate(case['points']):
                for hour, expected in enumerate(point['hourly_tecu_from_rounded_gim']):
                    row = rows[i*25+hour]
                    assert int(row['Year']) == year and int(row['DOY']) == doy
                    assert float(row['Hour_UT']) == hour
                    assert abs(float(row['VTEC_TECU'])-expected) <= reference['hourly_absolute_tolerance_tecu'] + 0.005
                    checked_hourly += 1
            # A batch may contain different UT requests at the same point/day.
            file.write_text('Year DOY ICI Longitude Latitude UT\n' + ''.join(
                f'{year} {doy} {ici} {point["longitude"]} {point["latitude"]} {ut}\n'
                for point in case['points'] for ut in point['ut']), encoding='utf-8')
            rows = run(['--input', file, '--modes', modes])
            expected = [value for point in case['points'] for value in point['fractional_tecu']]
            assert len(rows) == len(expected)
            for row, target in zip(rows, expected):
                assert row['VTEC_TECU'] == f'{target:.2f}'
                checked_fractional += 1
        # Named, positional, five-column, six-column, and --ut spellings agree.
        point = [2024, 1, 40.7, 116.4, 39.9]
        all_hours = run(point)
        single = run(point + [12.5])
        assert len(all_hours) == 25 and len(single) == 1
        assert run(point + ['--ut',12.5]) == single
        assert run(['--year',2024,'--doy',1,'--icivalue',40.7,'--lon',116.4,'--lat',39.9,'--ut',12.5]) == single
        for hour in [0,12,24]:
            row = run(point + ['--ut',hour])[0]
            assert float(row['VTEC_TECU']) == float(all_hours[hour]['VTEC_TECU'])
        # At any UT, interpolation equals the weighted grid-node values.
        ut=8.25
        corners=[run([2024,1,40.7,lon,lat,ut])[0] for lon,lat in [(115,40),(120,40),(115,37.5),(120,37.5)]]
        wx=(116.4-115)/5; wy=(40-39.9)/2.5
        expected=sum(w*float(row['VTEC_TECU']) for w,row in zip([(1-wx)*(1-wy),wx*(1-wy),(1-wx)*wy,wx*wy],corners))
        assert math.isclose(float(run(point+[ut])[0]['VTEC_TECU']),expected,rel_tol=0,abs_tol=0.01000001)
        # Continuous days share the UT24/UT00 value when next-day ICI is supplied.
        for year,doy in [(2024,59),(2024,60),(2024,366)]:
            following=dt.date(year,1,1)+dt.timedelta(days=doy)
            file=temp/'boundary.txt'
            file.write_text(f'{year} {doy} 40.7 116.4 39.9 24\n{following.year} {following.timetuple().tm_yday} 65 116.4 39.9 0\n')
            rows=run(['--input',file])
            assert rows[0]['VTEC_TECU']==rows[1]['VTEC_TECU']
        # Single input carries current ICI into the next day's model date.
        assert run([2024,366,40.7,116.4,39.9,24])[0]['VTEC_TECU']==run([2025,1,40.7,116.4,39.9,0])[0]['VTEC_TECU']
        bad_points=[[],[2024,1,40.7,0],[2023,366,40.7,0,0],[2024,1,-1,0,0],
                    [2024,1,'nan',0,0],[2024,1,40.7,180.01,0],[2024,1,40.7,0,87.6],
                    point+[-0.1],point+[24.1],point+['nan'],point+[12,'extra']]
        for bad in bad_points: run(bad,False)
        for bad in [['--modes',0],['--modes',19],['--unknown',1],['--ut'],['--lon',0],['--input',temp/'missing.txt']]:
            run(bad,False)
        file=temp/'conflict.txt'; file.write_text('2024 1 40.7 0 0 0\n2024 1 60 0 0 12\n')
        run(['--input',file],False)
        run(['--input',file,'--year',2024],False)
        file=temp/'bom.txt';file.write_text('\ufeff# comment\nYear DOY ICI Longitude Latitude UT\n2024 1 40.7 116.4 39.9 12.5 # comment\n',encoding='utf-8')
        assert run(['--input',file])==single
        output=temp/'tec.tsv'
        result=subprocess.run([str(executable),*map(str,point),'12.5','--data-dir',str(data),'--output',str(output)],capture_output=True,text=True)
        assert result.returncode==0 and result.stdout==''
        assert list(csv.DictReader(io.StringIO(output.read_text()),delimiter='\t'))==single
        # Existing destinations are protected; no model/input file can be truncated.
        old=output.read_bytes()
        run(point+['--output',output],False)
        assert output.read_bytes()==old
        # Two-decimal text output is not restricted to 0.1 TECU steps.
        assert any(abs(float(row['VTEC_TECU'])*10-round(float(row['VTEC_TECU'])*10))>1e-6 for row in single)
    ici_lines=(ROOT/'input/ici_2004_2024.txt').read_text().splitlines()[1:]
    assert len(ici_lines)==7671 and all(2004<=int(line.split()[0])<=2024 for line in ici_lines)
    dates=[tuple(map(int,line.split()[:2])) for line in ici_lines]
    assert len(set(dates))==7671 and dates[0]==(2004,1) and dates[-1]==(2024,366)
    print(f'PASS: {checked_hourly} hourly reference values, {checked_fractional} fractional-UT reference values, bilinear interpolation, boundaries, CLI/API conventions, input validation and ICI coverage.')

if __name__=='__main__':
    main()
