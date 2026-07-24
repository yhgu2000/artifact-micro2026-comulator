import json
from dataclasses import dataclass, field
from typing import Dict, Set, Optional


@dataclass(kw_only=True)
class FuncBwModel:
    outline: Optional[bool] = None
    analyze: Optional[bool] = None
    take_out: Optional[bool] = None
    chop_off: Optional[bool] = None

    def model_dump_json(self, indent: int = 2) -> str:
        # 实现序列化时的别名转换
        data = {}
        for key, value in self.__dict__.items():
            if value is not None:
                data[key.replace("_", "-")] = value
        return json.dumps(data, indent=indent, ensure_ascii=False)

    @classmethod
    def model_validate_json(cls, json_str: str) -> 'FuncBwModel':
        # 自定义反序列化，处理别名转换
        data = json.loads(json_str)
        # 将短横线转换为下划线
        converted_data = {}
        for key, value in data.items():
            converted_data[key.replace("-", "_")] = value
        return cls(**converted_data)


@dataclass(kw_only=True)
class BwListModel:
    Funcs: Dict[str, FuncBwModel] = field(default_factory=dict)
    Callees: Dict[str, bool] = field(default_factory=dict)
    FCPcmChop: Set[str] = field(default_factory=set)
    FCPcmChopOthers: bool = False

    def model_dump_json(self, indent: int = 2) -> str:
        # 自定义序列化，处理FuncBwModel对象
        data = {
            "Funcs": {k: json.loads(v.model_dump_json()) for k, v in self.Funcs.items()},
            "Callees": self.Callees,
            "FCPcmChop": list(self.FCPcmChop),  # 将set转换为list以支持JSON序列化
            "FCPcmChopOthers": self.FCPcmChopOthers
        }
        return json.dumps(data, indent=indent, ensure_ascii=False)

    @classmethod
    def model_validate_json(cls, json_str: str) -> 'BwListModel':
        # 自定义反序列化，处理FuncBwModel对象
        data = json.loads(json_str)
        if data.get('Funcs'):
            # 处理FuncBwModel对象，注意键是短横线格式
            funcs = {}
            for key, value in data['Funcs'].items():
                # 为FuncBwModel创建一个临时字典，将短横线转换为下划线
                func_data = {k.replace('-', '_'): v for k, v in value.items()}
                funcs[key] = FuncBwModel(**func_data)
            data['Funcs'] = funcs
        if 'FCPcmChop' in data:
            data['FCPcmChop'] = set(data['FCPcmChop'])
        return cls(**data)


@dataclass(kw_only=True)
class Config:
    Debug: bool = True
    LLorBC: bool = True
    MinBasicBlocks: int = 0
    MinInstructions: int = 0
    UsePFO: bool = False
    UseGRT: bool = False
    UseFCP: bool = False
    UseFCPcm: bool = False
    BwList: Optional[BwListModel] = None

    def model_dump_json(self, indent: int = 2) -> str:
        # 自定义序列化，处理BwListModel对象
        data = self.__dict__.copy()
        if data['BwList']:
            data['BwList'] = json.loads(data['BwList'].model_dump_json())
        return json.dumps(data, indent=indent, ensure_ascii=False)

    @classmethod
    def model_validate_json(cls, json_str: str) -> 'Config':
        # 自定义反序列化，处理BwListModel对象
        data = json.loads(json_str)
        if data.get('BwList'):
            # 使用BwListModel的model_validate_json方法来确保正确反序列化
            data['BwList'] = BwListModel.model_validate_json(json.dumps(data['BwList']))
        # 移除name字段，因为Config类没有这个字段
        data.pop('name', None)
        return cls(**data)